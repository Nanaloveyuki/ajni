import { AbilityConstant, UIAbility, Want } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { window } from '@kit.ArkUI';

const DOMAIN: number = 0xA1A1;
const TAG: string = 'AjniOhosMvp';

export default class EntryAbility extends UIAbility {
  private mode: string = 'suite';
  private runId: string = '';

  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    const params = want.parameters;
    if (params !== undefined) {
      if (typeof params['mode'] === 'string') {
        this.mode = params['mode'] as string;
      }
      if (typeof params['runId'] === 'string') {
        this.runId = params['runId'] as string;
      }
    }
    AppStorage.setOrCreate('ajniMode', this.mode);
    AppStorage.setOrCreate('ajniRunId', this.runId);
    hilog.info(DOMAIN, TAG, 'onCreate mode=%{public}s runId=%{public}s', this.mode, this.runId);
  }

  onDestroy(): void {
    hilog.info(DOMAIN, TAG, 'onDestroy mode=%{public}s runId=%{public}s', this.mode, this.runId);
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    windowStage.loadContent('pages/Index', (error) => {
      if (error.code) {
        hilog.error(DOMAIN, TAG, 'loadContent failed %{public}s', JSON.stringify(error));
        return;
      }
      hilog.info(DOMAIN, TAG, 'loadContent succeeded');
    });
  }

  onWindowStageDestroy(): void {
    hilog.info(DOMAIN, TAG, 'onWindowStageDestroy');
    AppStorage.setOrCreate('ajniStageDestroyed', true);
  }

  onForeground(): void {
    hilog.info(DOMAIN, TAG, 'onForeground');
    AppStorage.setOrCreate('ajniLifecycle', 'foreground');
  }

  onBackground(): void {
    hilog.info(DOMAIN, TAG, 'onBackground');
    AppStorage.setOrCreate('ajniLifecycle', 'background');
  }
}
