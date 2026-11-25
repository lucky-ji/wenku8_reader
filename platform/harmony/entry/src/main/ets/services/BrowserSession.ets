// SPDX-License-Identifier: GPL-2.0-only
import { webview } from '@kit.ArkWeb';
import { http } from '@kit.NetworkKit';
import { util } from '@kit.ArkTS';
import wenku8 from 'libwenku8_bridge.so';

export const strOrigin: string = 'https://www.wenku8.net';
export const strLoginUrl: string = strOrigin + '/login.php';
const nMaxBytes: number = 8 * 1024 * 1024;

export function isLibraryUrl(strUrl: string): boolean
{
  return strUrl === strOrigin || strUrl.startsWith(strOrigin + '/');
}

function isCoverHost(strUrl: string): boolean
{
  return strUrl.startsWith('https://img.wenku8.com/') ||
    strUrl.startsWith('https://pic.wenku8.com/') ||
    strUrl.startsWith('https://pic.777743.xyz/');
}

export class BrowserSession
{
  private readonly oController: webview.WebviewController;
  private readonly vPending: Set<number> = new Set<number>();
  private readonly oBase64: util.Base64Helper = new util.Base64Helper();

  constructor(oController: webview.WebviewController)
  {
    this.oController = oController;
  }

  request(strUrl: string, strMethod: string, strBody: string, nId: number,
    bFollow: boolean, bAsset: boolean): void
  {
    if (bAsset)
    {
      this.requestAsset(strUrl, strMethod, nId);
      return;
    }
    try
    {
      if (!isLibraryUrl(strUrl) || !isLibraryUrl(this.oController.getUrl()) ||
        (strMethod !== 'GET' && strMethod !== 'POST'))
      {
        wenku8.reject(nId);
        return;
      }
      this.vPending.add(nId);
      const strOptions: string = `{ mode: 'same-origin', credentials: 'same-origin', method: ${JSON.stringify(strMethod)},
        redirect: ${bFollow ? "'follow'" : "'manual'"},
        ${strMethod === 'POST' ? `headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: ${JSON.stringify(strBody)},` : ''}
        signal: controller.signal }`;
      const strScript: string = `(function () {
        const id = ${nId};
        const controller = new AbortController();
        const timer = setTimeout(() => controller.abort(), 16000);
        fetch(${JSON.stringify(strUrl)}, ${strOptions}).then(async response => {
          if (response.type === 'opaqueredirect') {
            window.Wenku8Transport.complete(id, 302, '', ${JSON.stringify(strUrl)}, '');
            return;
          }
          const bytes = new Uint8Array(await response.arrayBuffer());
          if (bytes.length > ${nMaxBytes}) { window.Wenku8Transport.failed(id); return; }
          let content = '';
          for (let i = 0; i < bytes.length; i += 8192) {
            content += String.fromCharCode.apply(null, bytes.subarray(i, i + 8192));
          }
          window.Wenku8Transport.complete(id, response.status, btoa(content),
            response.url, response.headers.get('content-type') || '');
        }).catch(() => window.Wenku8Transport.failed(id)).finally(() => clearTimeout(timer));
      }());`;
      this.oController.runJavaScript(strScript).catch(() =>
      {
        this.failed(nId);
      });
    }
    catch
    {
      this.vPending.delete(nId);
      wenku8.reject(nId);
    }
  }

  complete(nId: number, nStatus: number, strEncoded: string, strUrl: string,
    strContentType: string): void
  {
    if (!this.vPending.delete(nId))
    {
      return;
    }
    try
    {
      if (!isLibraryUrl(strUrl) || !isLibraryUrl(this.oController.getUrl()) ||
        strEncoded.length > Math.ceil(nMaxBytes / 3) * 4)
      {
        wenku8.reject(nId);
        return;
      }
      const vDecoded: Uint8Array = strEncoded.length > 0
        ? this.oBase64.decodeSync(strEncoded) : new Uint8Array(0);
      const oBytes = new ArrayBuffer(vDecoded.length);
      new Uint8Array(oBytes).set(vDecoded);
      wenku8.respond(nId, nStatus, oBytes, strUrl, strContentType);
    }
    catch
    {
      wenku8.reject(nId);
    }
  }

  failed(nId: number): void
  {
    if (this.vPending.delete(nId))
    {
      wenku8.reject(nId);
    }
  }

  invalidate(): void
  {
    this.vPending.forEach((nId: number) =>
    {
      wenku8.reject(nId);
    });
    this.vPending.clear();
  }

  private requestAsset(strUrl: string, strMethod: string, nId: number): void
  {
    if (strMethod !== 'GET' || !isCoverHost(strUrl))
    {
      wenku8.reject(nId);
      return;
    }
    const oRequest: http.HttpRequest = http.createHttp();
    oRequest.request(strUrl, {
      method: http.RequestMethod.GET,
      expectDataType: http.HttpDataType.ARRAY_BUFFER,
      usingCache: true,
      connectTimeout: 5000,
      readTimeout: 12000,
      maxLimit: nMaxBytes
    }).then((oResult: http.HttpResponse) =>
    {
      wenku8.respond(nId, oResult.responseCode, oResult.result as ArrayBuffer, strUrl);
    }).catch(() =>
    {
      wenku8.reject(nId);
    }).finally(() => oRequest.destroy());
  }
}
