// SPDX-License-Identifier: GPL-2.0-only
import { common } from '@kit.AbilityKit';
import { preferences } from '@kit.ArkData';
import { deviceInfo } from '@kit.BasicServicesKit';
import { webview } from '@kit.ArkWeb';
import { image } from '@kit.ImageKit';
import wenku8, {
  NativeNovel, NativeChapter, NativeVolume, NativeRequest, NativeResult
} from 'libwenku8_bridge.so';
import { BrowserSession, isLibraryUrl, strLoginUrl } from '../services/BrowserSession';

const vTitles: string[] = ['我的书架', '最近更新', '下载缓存', '我的'];
const vTabs: string[] = ['书架', '最近更新', '下载缓存', '我的'];
const vThemes: string[] = ['明亮', '暗色', '护眼'];

interface ReadingSpot
{
  nId: number;
  nChapter: number;
  nOffset: number;
  strTitle: string;
  strChapter: string;
}

interface ChapterBlock
{
  nId: number;
  strValue: string;
  bImage: boolean;
}

interface Bitmap
{
  nId: number;
  oImage: image.PixelMap;
}

class NativeFailure extends Error
{
  code: string = '';
}

@Entry
@Component
struct Index
{
  @State private nSection: number = 0;
  @State private strPage: string = 'library';
  @State private nWidth: number = 0;
  @State private nTheme: number = 0;
  @State private nFont: number = 19;
  @State private strAccount: string = '';
  @State private bLogin: boolean = false;
  @State private bReady: boolean = false;
  @State private bBusy: boolean = false;
  @State private bDownloading: boolean = false;
  @State private strStatus: string = '';
  @State private strDownload: string = '';
  @State private strQuery: string = '';
  @State private strSearch: string = '';
  @State private bShelfList: boolean = false;
  @State private bUpdateList: boolean = true;
  @State private vBooks: NativeNovel[] = [];
  @State private oBook: NativeNovel | undefined = undefined;
  @State private vVolumes: NativeVolume[] = [];
  @State private vChapters: NativeChapter[] = [];
  @State private strFilter: string = '';
  @State private nChapter: number = 0;
  @State private vBlocks: ChapterBlock[] = [];
  @State private vBitmaps: Bitmap[] = [];
  @State private vHistory: ReadingSpot[] = [];
  @State private vMarks: ReadingSpot[] = [];
  @State private bLocal: boolean = false;

  private strStorage: string = '';
  private oPreferences?: preferences.Preferences;
  private nPage: number = 0;
  private nLastPage: number = 0;
  private nOnlineTab: number = 0;
  private nProbeTimer: number = -1;
  private nSaveTimer: number = -1;
  private nReaderGeneration: number = 0;
  private bProbing: boolean = false;
  private bRestoring: boolean = false;
  private readonly oScroll: Scroller = new Scroller();
  private readonly oWeb: webview.WebviewController = new webview.WebviewController();
  private readonly oBrowser: BrowserSession = new BrowserSession(this.oWeb);

  aboutToAppear(): void
  {
    this.initialize();
  }

  aboutToDisappear(): void
  {
    this.saveSpot();
    clearInterval(this.nProbeTimer);
    clearTimeout(this.nSaveTimer);
    this.oBrowser.invalidate();
  }

  onBackPress(): boolean
  {
    if (this.bLogin || this.strPage !== 'library' || this.strSearch.length > 0)
    {
      this.back();
      return true;
    }
    return false;
  }

  private async initialize(): Promise<void>
  {
    try
    {
      const oContext = this.getUIContext().getHostContext() as common.UIAbilityContext;
      this.strStorage = oContext.filesDir;
      this.oPreferences = await preferences.getPreferences(oContext, 'wenku8_reader');
      this.bShelfList = await this.oPreferences.get('shelfList', false) as boolean;
      this.bUpdateList = await this.oPreferences.get('updateList', true) as boolean;
      this.nTheme = Number(await this.oPreferences.get('theme', 0));
      this.nFont = Number(await this.oPreferences.get('font', 19));
      this.vHistory = JSON.parse(await this.oPreferences.get('history', '[]') as string) as ReadingSpot[];
      this.vMarks = JSON.parse(await this.oPreferences.get('bookmarks', '[]') as string) as ReadingSpot[];
    }
    catch
    {
      this.strStatus = '暂时无法读取本地设置。';
    }
  }

  private async store(strKey: string, oValue: preferences.ValueType): Promise<void>
  {
    const oPreferences = this.oPreferences;
    if (oPreferences)
    {
      try
      {
        await oPreferences.put(strKey, oValue);
        await oPreferences.flush();
      }
      catch
      {
        this.strStatus = '暂时无法保存本地设置。';
      }
    }
  }

  private desktop(): boolean
  {
    return (deviceInfo.deviceType === '2in1' || deviceInfo.deviceType === 'pc') &&
      (this.nWidth === 0 || this.nWidth >= 720);
  }

  private bg(): string
  {
    return this.nTheme === 1 ? '#202630' : this.nTheme === 2 ? '#F3EDDD' : '#EFF3F8';
  }

  private panel(): string
  {
    return this.nTheme === 1 ? '#29313D' : this.nTheme === 2 ? '#FBF6E8' : '#FFFFFF';
  }

  private ink(): string
  {
    return this.nTheme === 1 ? '#EEF2F8' : this.nTheme === 2 ? '#344035' : '#233044';
  }

  private muted(): string
  {
    return this.nTheme === 1 ? '#B1BCCD' : '#586475';
  }

  private accent(): string
  {
    return this.nTheme === 1 ? '#A9D5BD' : '#315B46';
  }

  private online(): boolean
  {
    return this.bReady && this.strAccount.length > 0;
  }

  private request(oInput: NativeRequest, fnProgress?: (nDone: number, nTotal: number) => void): Promise<NativeResult>
  {
    return wenku8.run(oInput, (strUrl: string, strMethod: string, strBody: string, nId: number,
      bFollow: boolean, bAsset: boolean) =>
    {
      this.oBrowser.request(strUrl, strMethod, strBody, nId, bFollow, bAsset);
    }, fnProgress);
  }

  private showLogin(): void
  {
    if (this.bDownloading || this.bBusy)
    {
      return;
    }
    this.bLogin = true;
    this.strStatus = '';
    clearInterval(this.nProbeTimer);
    this.nProbeTimer = setInterval(() => this.probe(), 4500);
    try
    {
      this.oWeb.loadUrl(strLoginUrl);
    }
    catch
    {
      this.strStatus = '网页正在准备，请稍后重试。';
    }
  }

  private closeLogin(): void
  {
    this.bLogin = false;
    clearInterval(this.nProbeTimer);
    this.nProbeTimer = -1;
  }

  private async logout(): Promise<void>
  {
    if (this.bDownloading || this.bBusy)
    {
      return;
    }
    this.strAccount = '';
    this.vBooks = [];
    this.bReady = false;
    try
    {
      await webview.WebCookieManager.clearAllCookies();
      await webview.WebCookieManager.saveCookieAsync();
      this.oWeb.loadUrl(strLoginUrl);
    }
    catch
    {
      this.strStatus = '会话清理失败，请稍后重试。';
    }
  }

  private showFailure(oError: Object, strHint: string): void
  {
    if ((oError as NativeFailure).code === 'authentication')
    {
      this.strAccount = '';
      this.vBooks = [];
      this.nPage = 0;
      this.nLastPage = 0;
      this.strStatus = '登录状态已过期，请从「我的」重新登录。';
    }
    else
    {
      this.strStatus = strHint;
    }
  }

  private async probe(): Promise<void>
  {
    if (!this.bReady || this.bProbing || this.strAccount.length > 0)
    {
      return;
    }
    this.bProbing = true;
    try
    {
      const oResult = await this.request({ operation: 'profile', timeoutMs: 6500 });
      const strName = oResult.username ?? '';
      if (strName.length > 0)
      {
        this.strAccount = strName;
        if (this.bLogin)
        {
          this.closeLogin();
          this.selectTab(this.nOnlineTab);
        }
        else if (this.strPage === 'library' && this.nSection < 2)
        {
          this.loadLibrary(false);
        }
        await webview.WebCookieManager.saveCookieAsync().catch(() => {});
      }
    }
    catch
    {
      // The guest shell can read downloaded books while validation is unavailable.
    }
    finally
    {
      this.bProbing = false;
    }
  }

  private selectTab(nTab: number): void
  {
    if (this.bBusy)
    {
      return;
    }
    this.saveSpot();
    this.closeLogin();
    this.nSection = nTab;
    this.strPage = 'library';
    this.strQuery = '';
    this.strSearch = '';
    this.strStatus = '';
    this.nPage = 0;
    this.nLastPage = 0;
    this.vBooks = [];
    if (nTab < 2)
    {
      this.nOnlineTab = nTab;
    }
    if (nTab < 3)
    {
      this.loadLibrary(false);
    }
  }

  private search(): void
  {
    if (!this.bBusy && this.online() && this.strQuery.trim().length > 0)
    {
      this.strSearch = this.strQuery.trim();
      this.vBooks = [];
      this.loadLibrary(false);
    }
  }

  private async loadLibrary(bAppend: boolean): Promise<void>
  {
    if (this.bBusy || this.nSection > 2 || (this.nSection < 2 && !this.online()) ||
      (bAppend && (this.nSection !== 1 || this.strSearch.length > 0 || this.nPage >= this.nLastPage ||
        this.strStatus.length > 0)))
    {
      return;
    }
    const nPage = bAppend ? this.nPage + 1 : 1;
    this.bBusy = true;
    this.strStatus = '';
    try
    {
      let oResult: NativeResult;
      if (this.nSection === 2)
      {
        oResult = await wenku8.run({ operation: 'listCache', cacheDir: this.strStorage });
      }
      else if (this.strSearch.length > 0)
      {
        oResult = await this.request({ operation: 'search', query: this.strSearch });
      }
      else
      {
        oResult = await this.request({ operation: this.nSection === 0 ? 'shelf' : 'latest', page: nPage });
      }
      const vMerged: NativeNovel[] = bAppend ? this.vBooks.slice() : [];
      const vIds = new Set<number>(vMerged.map((oNovel: NativeNovel) => oNovel.id));
      (oResult.novels ?? []).forEach((oNovel: NativeNovel) =>
      {
        if (!vIds.has(oNovel.id))
        {
          vIds.add(oNovel.id);
          vMerged.push(oNovel);
        }
      });
      this.nLastPage = !bAppend || vMerged.length > this.vBooks.length ? oResult.pages ?? 0 : nPage;
      this.nPage = nPage;
      this.vBooks = vMerged;
    }
    catch (oError)
    {
      this.showFailure(oError, this.nSection === 2 ? '暂时无法读取下载缓存。' : '网络访问失败，请刷新重试。');
    }
    finally
    {
      this.bBusy = false;
    }
  }

  private setList(bList: boolean): void
  {
    if (this.nSection === 0)
    {
      this.bShelfList = bList;
      this.store('shelfList', bList);
    }
    else
    {
      this.bUpdateList = bList;
      this.store('updateList', bList);
    }
  }

  private async openBook(nId: number, oAnchor?: ReadingSpot): Promise<void>
  {
    if (this.bBusy)
    {
      return;
    }
    this.bBusy = true;
    this.strStatus = '';
    let bOpened = false;
    try
    {
      let oResult: NativeResult;
      try
      {
        oResult = await wenku8.run({ operation: 'cachedBook', id: nId, cacheDir: this.strStorage });
        this.bLocal = true;
      }
      catch
      {
        if (!this.online())
        {
          this.strStatus = '这本书尚无可读缓存，请从「我的」登录后阅读。';
          return;
        }
        oResult = await this.request({ operation: 'book', id: nId });
        this.bLocal = false;
      }
      if (oResult.novel)
      {
        this.oBook = oResult.novel;
        this.vVolumes = oResult.volumes ?? [];
        let vChapters: NativeChapter[] = [];
        this.vVolumes.forEach((oVolume: NativeVolume) =>
        {
          vChapters = vChapters.concat(oVolume.chapters);
        });
        this.vChapters = vChapters;
        this.strFilter = '';
        this.strPage = 'details';
        bOpened = true;
      }
    }
    catch (oError)
    {
      this.showFailure(oError, '暂时无法打开小说。');
    }
    finally
    {
      this.bBusy = false;
    }
    if (bOpened && oAnchor)
    {
      this.resume(oAnchor);
    }
  }

  private resume(oAnchor?: ReadingSpot): void
  {
    const oSpot = oAnchor ??
      this.vHistory.find((oValue: ReadingSpot) => oValue.nId === this.oBook?.id);
    const nIndex = oSpot ? this.vChapters.findIndex((oChapter: NativeChapter) =>
      oChapter.id === oSpot.nChapter) : -1;
    this.openChapter(nIndex >= 0 ? nIndex : 0, nIndex >= 0 ? oSpot?.nOffset ?? 0 : 0);
  }

  private async openChapter(nIndex: number, nOffset: number = 0): Promise<void>
  {
    const oBook = this.oBook;
    if (!oBook || this.bBusy || nIndex < 0 || nIndex >= this.vChapters.length)
    {
      return;
    }
    this.saveSpot();
    this.bBusy = true;
    this.strStatus = '';
    try
    {
      const oInput: NativeRequest = {
        operation: this.bLocal ? 'cachedChapter' : 'chapter', cacheDir: this.strStorage,
        id: oBook.id, chapterId: this.vChapters[nIndex].id
      };
      const oResult = this.bLocal ? await wenku8.run(oInput) : await this.request(oInput);
      const nGeneration = ++this.nReaderGeneration;
      this.nChapter = nIndex;
      this.vBlocks = this.splitChapter(oResult.text ?? '');
      this.vBitmaps = [];
      this.bRestoring = true;
      this.strPage = 'reader';
      setTimeout(() =>
      {
        if (this.strPage === 'reader' && nGeneration === this.nReaderGeneration)
        {
          this.oScroll.scrollTo({ xOffset: 0, yOffset: nOffset });
          this.bRestoring = false;
          this.saveSpot();
        }
      }, 150);
      if (this.bLocal)
      {
        this.loadImages(oBook.id, nGeneration);
      }
    }
    catch (oError)
    {
      this.showFailure(oError, '本章暂时无法读取，可返回目录重试。');
    }
    finally
    {
      this.bBusy = false;
    }
  }

  private splitChapter(strText: string): ChapterBlock[]
  {
    const vSegments = strText.split('<!--image-->');
    const vResult: ChapterBlock[] = [];
    vSegments.forEach((strSegment: string, nIndex: number) =>
    {
      const strValue = strSegment.trim();
      if (nIndex % 2 === 1 && nIndex + 1 < vSegments.length)
      {
        if (/^https?:\/\/(img\.wenku8\.com|pic\.wenku8\.com|pic\.777743\.xyz)\//.test(strValue))
        {
          vResult.push({ nId: vResult.length, strValue: strValue, bImage: true });
        }
      }
      else
      {
        strValue.split(/\r?\n/).forEach((strLine: string) =>
        {
          if (strLine.trim().length > 0)
          {
            vResult.push({ nId: vResult.length, strValue: strLine.trim(), bImage: false });
          }
        });
      }
    });
    return vResult;
  }

  private async loadImages(nId: number, nGeneration: number): Promise<void>
  {
    for (const oBlock of this.vBlocks)
    {
      if (!oBlock.bImage)
      {
        continue;
      }
      try
      {
        const oResult = await wenku8.run({
          operation: 'cachedImage', id: nId, url: oBlock.strValue, cacheDir: this.strStorage
        });
        if (oResult.bytes && nGeneration === this.nReaderGeneration)
        {
          const oSource = image.createImageSource(oResult.bytes);
          try
          {
            const oInfo = await oSource.getImageInfo();
            const nScale = Math.min(1, 900 / Math.max(oInfo.size.width, oInfo.size.height));
            const oBitmap = await oSource.createPixelMap({ desiredSize: {
              width: Math.max(1, Math.round(oInfo.size.width * nScale)),
              height: Math.max(1, Math.round(oInfo.size.height * nScale))
            } });
            if (nGeneration === this.nReaderGeneration && this.strPage === 'reader')
            {
              this.vBitmaps = this.vBitmaps.concat([{ nId: oBlock.nId, oImage: oBitmap }]);
            }
            else
            {
              await oBitmap.release();
            }
          }
          finally
          {
            await oSource.release();
          }
        }
      }
      catch
      {
        // A missing illustration does not prevent text reading.
      }
      if (nGeneration !== this.nReaderGeneration || this.strPage !== 'reader')
      {
        return;
      }
    }
  }

  private getSpot(): ReadingSpot | undefined
  {
    const oBook = this.oBook;
    const oChapter = this.vChapters[this.nChapter];
    if (this.strPage !== 'reader' || this.bRestoring || !oBook || !oChapter)
    {
      return undefined;
    }
    return { nId: oBook.id, nChapter: oChapter.id, nOffset: Math.max(0, this.oScroll.currentOffset().yOffset),
      strTitle: oBook.title, strChapter: oChapter.title };
  }

  private saveSpot(): void
  {
    const oSpot = this.getSpot();
    if (oSpot)
    {
      this.vHistory = [oSpot].concat(this.vHistory.filter((oOld: ReadingSpot) => oOld.nId !== oSpot.nId))
        .slice(0, 100);
      this.store('history', JSON.stringify(this.vHistory));
    }
  }

  private bookmark(): void
  {
    const oSpot = this.getSpot();
    if (oSpot)
    {
      this.vMarks = [oSpot].concat(this.vMarks).slice(0, 100);
      this.store('bookmarks', JSON.stringify(this.vMarks));
      this.strStatus = '已添加书签。';
    }
  }

  private async download(): Promise<void>
  {
    const oBook = this.oBook;
    if (!oBook || this.bDownloading || !this.online())
    {
      return;
    }
    this.bDownloading = true;
    this.strDownload = '正在缓存「' + oBook.title + '」';
    try
    {
      await this.request({ operation: 'download', cacheDir: this.strStorage, id: oBook.id,
        account: this.strAccount, includeImages: true }, (nDone: number, nTotal: number) =>
      {
        this.strDownload = '缓存进度 ' + nDone.toString() + ' / ' + nTotal.toString();
      });
      this.strDownload = '已缓存「' + oBook.title + '」';
      if (this.strPage === 'library' && this.nSection === 2)
      {
        this.loadLibrary(false);
      }
    }
    catch
    {
      this.strDownload = '缓存暂未完成，重新开始可继续已有进度。';
    }
    finally
    {
      this.bDownloading = false;
    }
  }

  private async addShelf(): Promise<void>
  {
    const oBook = this.oBook;
    if (!oBook || this.bBusy || !this.online())
    {
      return;
    }
    this.bBusy = true;
    try
    {
      await this.request({ operation: 'addToShelf', id: oBook.id, account: this.strAccount });
      this.strStatus = '已加入书架。';
    }
    catch (oError)
    {
      this.showFailure(oError, '加入书架失败，请稍后重试。');
    }
    finally
    {
      this.bBusy = false;
    }
  }

  private back(): void
  {
    if (this.bLogin)
    {
      this.closeLogin();
    }
    else if (!this.bBusy)
    {
      this.saveSpot();
      this.strStatus = '';
      if (this.strPage === 'reader')
      {
        ++this.nReaderGeneration;
        this.vBitmaps = [];
        this.strPage = 'details';
      }
      else if (this.strPage === 'details')
      {
        this.strPage = 'library';
      }
      else if (this.strSearch.length > 0)
      {
        this.selectTab(this.nSection);
      }
    }
  }

  private coverUrl(nId: number): string
  {
    return 'https://img.wenku8.com/image/' + Math.floor(nId / 1000).toString() +
      '/' + nId.toString() + '/' + nId.toString() + 's.jpg';
  }

  private columns(): string
  {
    const nContent = this.desktop() ? this.nWidth - 218 : this.nWidth;
    return '1fr '.repeat(Math.max(2, Math.min(5, Math.floor(nContent / 170)))).trim();
  }

  private matches(oVolume: NativeVolume, oChapter: NativeChapter): boolean
  {
    const strFilter = this.strFilter.trim().toLowerCase();
    return strFilter.length === 0 || oVolume.title.toLowerCase().includes(strFilter) ||
      oChapter.title.toLowerCase().includes(strFilter);
  }

  @Builder
  private nav(bDesktop: boolean)
  {
    if (bDesktop)
    {
      Column({ space: 8 })
      {
        Text('轻小说文库').fontSize(22).fontWeight(FontWeight.Bold).fontColor(this.ink())
          .width('100%').padding({ left: 8, bottom: 17 });
        ForEach(vTitles, (strLabel: string, nTab: number) =>
        {
          Button(strLabel, { type: ButtonType.Normal }).width('100%').height(45).borderRadius(9)
            .fontColor(this.nSection === nTab ? '#FFFFFF' : this.ink())
            .backgroundColor(this.nSection === nTab ? '#315B46' : this.panel())
            .onClick(() => this.selectTab(nTab));
        }, (strLabel: string) => strLabel);
        Blank();
      }.width(218).height('100%').padding(16).backgroundColor(this.panel());
    }
    else
    {
      Row({ space: 4 })
      {
        ForEach(vTabs, (strLabel: string, nTab: number) =>
        {
          Button(strLabel, { type: ButtonType.Normal }).height(48).layoutWeight(1).borderRadius(9)
            .fontSize(13).fontColor(this.nSection === nTab ? this.accent() : this.muted())
            .backgroundColor(this.nSection === nTab ? this.bg() : this.panel())
            .onClick(() => this.selectTab(nTab));
        }, (strLabel: string) => strLabel);
      }.width('100%').padding(6).backgroundColor(this.panel());
    }
  }

  @Builder
  private metadata(oNovel: NativeNovel)
  {
    if (oNovel.author.length > 0)
    {
      Text(oNovel.author).fontSize(13).fontColor(this.muted()).maxLines(1)
        .textOverflow({ overflow: TextOverflow.Ellipsis });
    }
    if (oNovel.status.length > 0)
    {
      Text(oNovel.status).fontSize(12).fontColor(this.accent()).backgroundColor(this.bg())
        .padding({ left: 7, right: 7, top: 3, bottom: 3 }).borderRadius(6);
    }
  }

  @Builder
  private cover(oNovel: NativeNovel, bGrid: boolean)
  {
    Stack()
    {
      Text('书').fontSize(24).fontColor(this.muted());
      if (this.online() && this.nSection < 2)
      {
        Image(this.coverUrl(oNovel.id)).width('100%').height('100%').objectFit(ImageFit.Cover);
      }
    }.width(bGrid ? '100%' : 62).height(bGrid ? 143 : 83)
      .backgroundColor(this.bg()).clip(true).borderRadius(8);
  }

  @Builder
  private card(oNovel: NativeNovel, bGrid: boolean)
  {
    if (bGrid)
    {
      Column({ space: 6 })
      {
        this.cover(oNovel, true);
        Text(oNovel.title).fontSize(15).fontColor(this.ink()).fontWeight(FontWeight.Medium).maxLines(2)
          .width('100%').textOverflow({ overflow: TextOverflow.Ellipsis });
        this.metadata(oNovel);
        Blank();
      }.alignItems(HorizontalAlign.Start).width('100%').height(240).padding(9).borderRadius(12)
        .backgroundColor(this.panel()).onClick(() => this.openBook(oNovel.id));
    }
    else
    {
      Row({ space: 13 })
      {
        this.cover(oNovel, false);
        Column({ space: 5 })
        {
          Text(oNovel.title).fontSize(16).fontWeight(FontWeight.Medium).fontColor(this.ink()).maxLines(2)
            .textOverflow({ overflow: TextOverflow.Ellipsis });
          this.metadata(oNovel);
        }.alignItems(HorizontalAlign.Start).layoutWeight(1);
      }.width('100%').padding(9).borderRadius(11).backgroundColor(this.panel())
        .onClick(() => this.openBook(oNovel.id));
    }
  }

  @Builder
  private library()
  {
    Column({ space: 10 })
    {
      Text(this.strSearch.length > 0 ? '搜索结果' : vTitles[this.nSection])
        .fontSize(this.desktop() ? 27 : 22).fontWeight(FontWeight.Bold).fontColor(this.ink()).width('100%');
      if (this.nSection < 2 && !this.online())
      {
        Column({ space: 12 })
        {
          Text(this.nSection === 0 ? '登录后查看书架' : '登录后查看最近更新')
            .fontSize(19).fontWeight(FontWeight.Medium).fontColor(this.ink());
          Text('已下载的小说可以离线续读。').fontSize(14).fontColor(this.muted());
          Button('前往我的').onClick(() => this.selectTab(3));
          Button('打开下载缓存').onClick(() => this.selectTab(2));
        }.alignItems(HorizontalAlign.Start).width('100%').padding(18)
          .borderRadius(14).backgroundColor(this.panel());
        Blank();
      }
      else
      {
        if (this.nSection < 2)
        {
          Row({ space: 7 })
          {
            TextInput({ text: this.strQuery, placeholder: '搜索书名' }).layoutWeight(1)
              .onChange((strValue: string) => { this.strQuery = strValue; })
              .onSubmit(() => this.search());
            Button('搜索').enabled(!this.bBusy).onClick(() => this.search());
          }.width('100%');
        }
        Row({ space: 6 })
        {
          Text(this.bBusy ? '正在加载…' : this.vBooks.length.toString() + ' 本小说')
            .fontSize(13).fontColor(this.muted()).layoutWeight(1);
          if (this.nSection < 2 && this.strSearch.length === 0)
          {
            Button('平铺').enabled(!this.bBusy).onClick(() => this.setList(false));
            Button('列表').enabled(!this.bBusy).onClick(() => this.setList(true));
          }
          if (this.strSearch.length > 0)
          {
            Button('退出搜索').enabled(!this.bBusy).onClick(() => this.selectTab(this.nSection));
          }
          Button('刷新').enabled(!this.bBusy).onClick(() => this.loadLibrary(false));
        }.width('100%');
        if (!this.bBusy && this.vBooks.length === 0)
        {
          Text(this.nSection === 2 ? '还没有已下载的小说。' : '暂无可显示的小说。')
            .fontColor(this.muted()).padding(11);
        }
        if (this.nSection < 2 && this.strSearch.length === 0 &&
          !(this.nSection === 0 ? this.bShelfList : this.bUpdateList))
        {
          Grid()
          {
            ForEach(this.vBooks, (oNovel: NativeNovel) =>
            {
              GridItem() { this.card(oNovel, true); }
            }, (oNovel: NativeNovel) => oNovel.id.toString());
          }.columnsTemplate(this.columns()).columnsGap(9).rowsGap(9).width('100%').layoutWeight(1)
            .onReachEnd(() => this.loadLibrary(true));
        }
        else
        {
          List({ space: 7 })
          {
            ForEach(this.vBooks, (oNovel: NativeNovel) =>
            {
              ListItem() { this.card(oNovel, false); }
            }, (oNovel: NativeNovel) => oNovel.id.toString());
          }.width('100%').layoutWeight(1).onReachEnd(() => this.loadLibrary(true));
        }
      }
    }.width('100%').height('100%');
  }

  @Builder
  private details()
  {
    Column({ space: 9 })
    {
      Row({ space: 8 })
      {
        Button('返回').onClick(() => this.back());
        Text(this.oBook?.title ?? '小说详情').fontSize(20).fontWeight(FontWeight.Bold)
          .fontColor(this.ink()).maxLines(2).textOverflow({ overflow: TextOverflow.Ellipsis }).layoutWeight(1);
      }.width('100%');
      if (this.oBook)
      {
        Row({ space: 9 })
        {
          this.metadata(this.oBook);
          if (this.bLocal)
          {
            Text('已缓存').fontSize(13).fontColor(this.accent());
          }
        }.width('100%');
        if (this.oBook.intro.length > 0)
        {
          Text(this.oBook.intro).fontColor(this.muted()).fontSize(14).maxLines(3)
            .width('100%').textOverflow({ overflow: TextOverflow.Ellipsis });
        }
      }
      Row({ space: 7 })
      {
        Button('开始／继续').enabled(!this.bBusy && this.vChapters.length > 0).onClick(() => this.resume());
        if (this.online())
        {
          Button('加入书架').enabled(!this.bBusy).onClick(() => this.addShelf());
          if (!this.bLocal)
          {
            Button('下载缓存').enabled(!this.bDownloading).onClick(() => this.download());
          }
        }
      }.width('100%');
      TextInput({ text: this.strFilter, placeholder: '筛选卷名或章名' }).width('100%')
        .onChange((strValue: string) => { this.strFilter = strValue; });
      List({ space: 5 })
      {
        ForEach(this.vVolumes, (oVolume: NativeVolume, nVolume: number) =>
        {
          if (oVolume.chapters.some((oChapter: NativeChapter) => this.matches(oVolume, oChapter)))
          {
            ListItem()
            {
              Text(oVolume.title).fontSize(16).fontWeight(FontWeight.Medium).fontColor(this.ink())
                .width('100%').padding(9).backgroundColor(this.panel()).borderRadius(7);
            }
            ForEach(oVolume.chapters.filter((oChapter: NativeChapter) => this.matches(oVolume, oChapter)),
              (oChapter: NativeChapter) =>
              {
                ListItem()
                {
                  Text(oChapter.title).fontSize(15).fontColor(this.ink()).width('100%').padding(11)
                    .onClick(() => this.openChapter(
                      this.vChapters.findIndex((oEntry: NativeChapter) => oEntry.id === oChapter.id)));
                }
              }, (oChapter: NativeChapter) => nVolume.toString() + '/' + oChapter.id.toString());
          }
        }, (oVolume: NativeVolume, nVolume: number) => oVolume.id.toString() + '/' + nVolume.toString());
      }.width('100%').layoutWeight(1);
    }.width('100%').height('100%');
  }

  @Builder
  private reader()
  {
    Column({ space: 8 })
    {
      Row({ space: 8 })
      {
        Button('目录').onClick(() => this.back());
        Text(this.oBook?.title ?? '').fontSize(15).fontColor(this.ink()).maxLines(1)
          .layoutWeight(1).textOverflow({ overflow: TextOverflow.Ellipsis });
        Button('书签').onClick(() => this.bookmark());
      }.width('100%');
      Scroll(this.oScroll)
      {
        Column({ space: 12 })
        {
          Text(this.vChapters[this.nChapter]?.title ?? '').fontSize(this.nFont + 6)
            .fontColor(this.ink()).fontWeight(FontWeight.Bold).width('100%').padding({ bottom: 9 });
          ForEach(this.vBlocks, (oBlock: ChapterBlock) =>
          {
            if (!oBlock.bImage)
            {
              Text(oBlock.strValue).fontSize(this.nFont).fontColor(this.ink()).width('100%')
                .lineHeight(this.nFont * 1.65).textIndent(this.nFont * 2);
            }
            else if (this.bLocal)
            {
              ForEach(this.vBitmaps.filter((oBitmap: Bitmap) => oBitmap.nId === oBlock.nId), (oBitmap: Bitmap) =>
              {
                Image(oBitmap.oImage).width('100%').height(300).objectFit(ImageFit.Contain);
              }, (oBitmap: Bitmap) => oBitmap.nId.toString());
            }
            else
            {
              Image(oBlock.strValue.replace(/^http:\/\//, 'https://'))
                .width('100%').height(300).objectFit(ImageFit.Contain);
            }
          }, (oBlock: ChapterBlock) => this.nChapter.toString() + '/' + oBlock.nId.toString());
        }.padding(16).width('100%').constraintSize({ maxWidth: 940 });
      }.width('100%').layoutWeight(1).backgroundColor(this.panel()).borderRadius(11)
        .onDidScroll(() =>
        {
          if (!this.bRestoring)
          {
            clearTimeout(this.nSaveTimer);
            this.nSaveTimer = setTimeout(() => this.saveSpot(), 330);
          }
        });
      Row({ space: 8 })
      {
        Button('上一章').enabled(this.nChapter > 0 && !this.bBusy)
          .onClick(() => this.openChapter(this.nChapter - 1));
        Text((this.nChapter + 1).toString() + ' / ' + this.vChapters.length.toString())
          .textAlign(TextAlign.Center).fontColor(this.muted()).layoutWeight(1);
        Button('下一章').enabled(this.nChapter + 1 < this.vChapters.length && !this.bBusy)
          .onClick(() => this.openChapter(this.nChapter + 1));
      }.width('100%');
    }.width('100%').height('100%');
  }

  @Builder
  private account()
  {
    Scroll()
    {
      Column({ space: 12 })
      {
        Text('我的').fontSize(24).fontWeight(FontWeight.Bold).fontColor(this.ink());
        Text(this.strAccount.length > 0 ? this.strAccount : '尚未登录').fontSize(16).fontColor(this.ink());
        if (this.strAccount.length > 0)
        {
          Button('退出登录').enabled(!this.bDownloading && !this.bBusy).onClick(() => this.logout());
        }
        else
        {
          Button('登录文库').enabled(!this.bDownloading && !this.bBusy).onClick(() => this.showLogin());
        }
        Text('阅读外观').fontWeight(FontWeight.Medium).fontColor(this.ink());
        Row({ space: 7 })
        {
          ForEach(vThemes, (strTheme: string, nChoice: number) =>
          {
            Button(strTheme).onClick(() =>
            {
              this.nTheme = nChoice;
              this.store('theme', nChoice);
            });
          }, (strTheme: string) => strTheme);
        };
        Row({ space: 8 })
        {
          Button('字号 −').onClick(() =>
          {
            this.nFont = Math.max(13, this.nFont - 1);
            this.store('font', this.nFont);
          });
          Text(this.nFont.toString()).fontColor(this.ink());
          Button('字号 +').onClick(() =>
          {
            this.nFont = Math.min(36, this.nFont + 1);
            this.store('font', this.nFont);
          });
        };
        Text('阅读历史').fontWeight(FontWeight.Medium).fontColor(this.ink());
        ForEach(this.vHistory, (oSpot: ReadingSpot) =>
        {
          Text(oSpot.strTitle + ' · ' + oSpot.strChapter).fontColor(this.ink()).maxLines(2).width('100%')
            .padding(9).backgroundColor(this.panel()).borderRadius(8)
            .onClick(() => this.openBook(oSpot.nId, oSpot));
        }, (oSpot: ReadingSpot) => oSpot.nId.toString());
        Text('书签').fontWeight(FontWeight.Medium).fontColor(this.ink());
        ForEach(this.vMarks, (oSpot: ReadingSpot, nIndex: number) =>
        {
          Row({ space: 7 })
          {
            Text(oSpot.strTitle + ' · ' + oSpot.strChapter).fontColor(this.ink()).maxLines(2).layoutWeight(1)
              .onClick(() => this.openBook(oSpot.nId, oSpot));
            Button('删除').onClick(() =>
            {
              this.vMarks = this.vMarks.filter((_oEntry: ReadingSpot, nOther: number) => nOther !== nIndex);
              this.store('bookmarks', JSON.stringify(this.vMarks));
            });
          }.width('100%').padding(8).backgroundColor(this.panel()).borderRadius(8);
        }, (_oSpot: ReadingSpot, nIndex: number) => nIndex.toString());
        Text('轻小说文库 1.0.7 · SDK ' + wenku8.sdkVersion()).fontSize(13).fontColor(this.muted());
        Text('https://github.com/lucky-ji/wenku8_reader').fontSize(13).fontColor(this.accent())
          .copyOption(CopyOptions.LocalDevice);
      }.alignItems(HorizontalAlign.Start).padding(6).width('100%');
    }.width('100%').height('100%');
  }

  @Builder
  private content()
  {
    Column({ space: 5 })
    {
      Column()
      {
        if (this.strPage === 'reader')
        {
          this.reader();
        }
        else if (this.strPage === 'details')
        {
          this.details();
        }
        else if (this.nSection === 3)
        {
          this.account();
        }
        else
        {
          this.library();
        }
      }.layoutWeight(1).width('100%');
      if (this.strStatus.length > 0 || this.strDownload.length > 0)
      {
        Text(this.strStatus.length > 0 ? this.strStatus : this.strDownload)
          .fontSize(13).fontColor(this.muted()).width('100%').padding(5);
      }
    }.height('100%').width('100%').padding(this.desktop() ? 19 : 9);
  }

  build()
  {
    Stack()
    {
      if (this.desktop())
      {
        Row()
        {
          this.nav(true);
          Column() { this.content(); }.height('100%').layoutWeight(1);
        }.width('100%').height('100%');
      }
      else
      {
        Column()
        {
          Column() { this.content(); }.width('100%').layoutWeight(1);
          if (this.strPage === 'library')
          {
            this.nav(false);
          }
        }.width('100%').height('100%');
      }
      Column()
      {
        if (this.bLogin)
        {
          Row({ space: 10 })
          {
            Button('返回').onClick(() => this.closeLogin());
            Text('登录文库').fontSize(18).fontWeight(FontWeight.Medium).fontColor(this.ink()).layoutWeight(1);
          }.width('100%').padding(10);
        }
        Web({ src: strLoginUrl, controller: this.oWeb }).width('100%').layoutWeight(1)
          .javaScriptAccess(true).domStorageAccess(true).fileAccess(false).mixedMode(MixedMode.None)
          .javaScriptProxy({ object: this.oBrowser, name: 'Wenku8Transport',
            methodList: ['complete', 'failed'], controller: this.oWeb })
          .onLoadIntercept((oEvent: OnLoadInterceptEvent) =>
            oEvent.data.isMainFrame() && !isLibraryUrl(oEvent.data.getRequestUrl()))
          .onPageBegin(() =>
          {
            this.bReady = false;
            this.oBrowser.invalidate();
          })
          .onPageEnd((oEvent: OnPageEndEvent) =>
          {
            this.bReady = isLibraryUrl(oEvent.url);
            this.probe();
          })
          .onErrorReceive((oEvent: OnErrorReceiveEvent) =>
          {
            if (this.bLogin && oEvent.request.isMainFrame())
            {
              this.strStatus = '登录页无法加载，请检查网络或应用联网权限。';
            }
          });
        if (this.bLogin && this.strStatus.length > 0)
        {
          Text(this.strStatus).fontSize(13).fontColor(this.muted()).padding(8);
        }
      }.width(this.bLogin ? '100%' : 1).height(this.bLogin ? '100%' : 1)
        .opacity(this.bLogin ? 1 : 0).backgroundColor(this.panel())
        .hitTestBehavior(this.bLogin ? HitTestMode.Default : HitTestMode.None);
    }.width('100%').height('100%').backgroundColor(this.bg())
      .onSizeChange((_oOld: SizeOptions, oNew: SizeOptions) =>
      {
        this.nWidth = Number(oNew.width);
      });
  }
}
