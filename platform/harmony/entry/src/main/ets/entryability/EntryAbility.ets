// SPDX-License-Identifier: GPL-2.0-only
import { UIAbility } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import { hilog } from '@kit.PerformanceAnalysisKit';

export default class EntryAbility extends UIAbility
{
  onWindowStageCreate(oStage: window.WindowStage): void
  {
    try
    {
      oStage.loadContent('pages/Index').catch(() =>
      {
        hilog.error(0x711, 'Wenku8', 'Unable to load reader page');
      });
    }
    catch
    {
      hilog.error(0x711, 'Wenku8', 'Unable to initialize reader page');
    }
  }
}
