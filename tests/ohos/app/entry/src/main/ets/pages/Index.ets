import { hilog } from '@kit.PerformanceAnalysisKit';
import worker, { MessageEvents, ErrorEvent } from '@ohos.worker';
import probe, { Snapshot, Diagnostics, RoundTrip } from 'libajni_ohos_probe.so';
import { WorkerCommand, WorkerReply } from '../workers/Protocol';

const DOMAIN: number = 0xA1A1;
const TAG: string = 'AjniOhosMvp';
const TIMEOUT_MS: number = 12000;

interface ScenarioResult {
  name: string;
  passed: boolean;
  detail: string;
}

interface NativeError extends Error {
  code?: string;
}

interface ScenarioReport {
  mode: string;
  runId: string;
  scenarios?: ScenarioResult[];
  diagnostics?: Diagnostics;
  failure?: string;
  diagnosticsFailure?: string;
  worker?: string;
}

function assert(condition: boolean, message: string): void {
  if (!condition) {
    throw new Error(message);
  }
}

function equal<T>(actual: T, expected: T, message: string): void {
  assert(actual === expected, `${message}: expected ${String(expected)}, got ${String(actual)}`);
}

function bytes(values: number[]): ArrayBuffer {
  return new Uint8Array(values).buffer;
}

function byteValues(value: ArrayBuffer): number[] {
  return Array.from(new Uint8Array(value));
}

function expectThrows(action: () => void, code: string, message: string): Error {
  try {
    action();
  } catch (error) {
    const caught = error as NativeError;
    equal(caught.code, code, `${message} code`);
    return caught;
  }
  throw new Error(`${message}: call unexpectedly succeeded`);
}

function appendResult(results: ScenarioResult[], name: string, action: () => void): void {
  try {
    action();
    results.push({ name: name, passed: true, detail: 'passed' });
  } catch (error) {
    results.push({ name: name, passed: false, detail: String(error) });
    throw error as Error;
  }
}

function waitForSnapshot(token: number, condition: (value: Snapshot) => boolean, label: string): Promise<Snapshot> {
  return new Promise<Snapshot>((resolve, reject) => {
    const started: number = Date.now();
    const poll = (): void => {
      try {
        const current: Snapshot = probe.snapshot(token);
        if (condition(current)) {
          resolve(current);
          return;
        }
        if (Date.now() - started >= TIMEOUT_MS) {
          reject(new Error(`${label} timed out; snapshot=${JSON.stringify(current)}`));
          return;
        }
        setTimeout(poll, 25);
      } catch (error) {
        reject(error);
      }
    };
    poll();
  });
}

@Entry
@Component
struct Index {
  @State private status: string = 'Ready to exercise the real HarmonyOS Node-API runtime.';
  @State private results: ScenarioResult[] = [];
  @State private busy: boolean = false;
  @State private mode: string = 'suite';
  @State private runId: string = '';
  @State private cleanupWorker: worker.ThreadWorker | undefined = undefined;
  @State private cleanupStatus: string = '';
  private token: number = 0;
  private resultsRef: ScenarioResult[] = [];

  aboutToAppear(): void {
    this.mode = AppStorage.get<string>('ajniMode') ?? 'suite';
    this.runId = AppStorage.get<string>('ajniRunId') ?? '';
    if (this.mode === 'cleanup') {
      this.beginCleanupMode();
    } else if (this.mode === 'suite') {
      this.runSuite();
    }
  }

  aboutToDisappear(): void {
    if (this.cleanupWorker !== undefined) {
      try {
        this.cleanupWorker.terminate();
        this.cleanupWorker = undefined;
      } catch (error) {
        hilog.error(DOMAIN, TAG, 'UI teardown Worker termination failed: %{public}s', String(error));
      }
    }
    if (this.token !== 0) {
      try {
        probe.close(this.token);
        this.token = 0;
      } catch (error) {
        hilog.error(DOMAIN, TAG, 'UI teardown close failed: %{public}s', String(error));
      }
    }
  }

  private logResult(passed: boolean, report: object): void {
    const marker: string = `AJNI_OHOS_MVP ${passed ? 'PASS' : 'FAIL'} ${JSON.stringify(report)}`;
    if (passed) {
      hilog.info(DOMAIN, TAG, '%{public}s', marker);
    } else {
      hilog.error(DOMAIN, TAG, '%{public}s', marker);
    }
  }
  private async runWorkerBatch(token: number, count: number): Promise<Snapshot> {
    probe.startWorker(token, count);
    return await waitForSnapshot(token, (state) => state.delivered === count && !state.workerRunning, 'worker batch');
  }

  private record(name: string, action: () => void): void {
    appendResult(this.resultsRef, name, action);
    this.results = this.resultsRef.slice();
  }

  private callbackFor(expectedKind: number, callback: (kind: number, value: number, text: string, data: ArrayBuffer) => number): (kind: number, value: number, text: string, data: ArrayBuffer) => number {
    return (kind: number, value: number, text: string, data: ArrayBuffer): number => {
      equal(kind, expectedKind, 'callback kind');
      return callback(kind, value, text, data);
    };
  }

  private async runSuite(): Promise<void> {
    if (this.busy) {
      return;
    }
    this.busy = true;
    this.resultsRef = [];
    this.results = [];
    this.status = 'Running native runtime scenarios…';
    const report: ScenarioReport = { mode: 'suite', runId: this.runId, scenarios: [] };
    let failed: string = '';
    try {
      const expectedText: string = 'NUL\u0000-CJK-漢字-emoji-🙂-';
      const expectedData: number[] = [0, 0x80, 0xff, 0x41];
      let callbackCalls: number = 0;
      let reentered: boolean = false;
      let callbackMode: string = 'normal';
      const originalCallbackError: Error = new Error('preserve callback exception identity');
      let busySeen: boolean = false;
      this.token = probe.open(this.callbackFor(0, (_kind, value, text, data) => {
        callbackCalls += 1;
        if (callbackMode === 'throw') {
          throw originalCallbackError;
        }
        if (callbackMode === 'close-busy') {
          try {
            probe.close(this.token);
          } catch (error) {
            busySeen = (error as NativeError).code === 'Busy';
          }
          return 12;
        }
        if (value === 5) {
          equal(text, expectedText, 'roundTrip callback text');
          assert(text.includes('\u0000') && text.includes('漢字') && text.includes('🙂'), 'callback text must retain UTF-16 edge cases');
          equal(JSON.stringify(byteValues(data)), JSON.stringify(expectedData), 'roundTrip callback bytes');
          if (!reentered) {
            reentered = true;
            const nested: RoundTrip = probe.roundTrip(this.token, 1, 'nested', bytes([0x80]));
            equal(nested.total, 6, 'nested managed total');
          }
        }
        return 73;
      }));

      this.record('same-owner synchronous callback reentry preserves outer snapshot', () => {
        const before: Snapshot = probe.snapshot(this.token);
        equal(before.total, 0, 'initial total');
        equal(before.managedEntries, 0, 'initial managed entries');
        const result: RoundTrip = probe.roundTrip(this.token, 5, expectedText, bytes(expectedData));
        equal(result.total, 5, 'outer total snapshot');
        equal(result.callbackResult, 73, 'returned callback result');
        equal(result.text, expectedText, 'returned callback text');
        equal(JSON.stringify(byteValues(result.bytes)), JSON.stringify(expectedData), 'returned callback bytes');
        const afterNested: Snapshot = probe.snapshot(this.token);
        equal(afterNested.total, 6, 'nested reentry total');
        equal(afterNested.managedEntries, 2, 'nested reentry managed entries');
        equal(callbackCalls, 2, 'outer and nested callback count');
        assert(reentered, 'synchronous callback reentered same owner session');
      });

      this.record('stateful follow-up round trip', () => {
        const result: RoundTrip = probe.roundTrip(this.token, 2, 'outer', bytes([9]));
        equal(result.total, 8, 'follow-up outer snapshot');
        equal(result.callbackResult, 73, 'follow-up callback result');
        equal(probe.snapshot(this.token).total, 8, 'follow-up managed total');
      });

      this.record('negative delta is recoverable and state-preserving', () => {
        expectThrows(() => probe.roundTrip(this.token, -1, 'invalid', bytes([])), 'MoonBitError', 'negative delta');
        const after: Snapshot = probe.snapshot(this.token);
        equal(after.total, 8, 'total after rejected delta');
        equal(after.managedEntries, 4, 'negative delta managed entry accounting');
        const recovered: RoundTrip = probe.roundTrip(this.token, 1, 'recovered', bytes([1]));
        equal(recovered.total, 9, 'recovery total');
      });

      this.record('callback exception identity and recovery', () => {
        callbackMode = 'throw';
        let caught: Error | undefined = undefined;
        try {
          probe.roundTrip(this.token, 1, 'throw', bytes([0xff]));
        } catch (error) {
          caught = error as Error;
        }
        assert(caught === originalCallbackError, 'native callback must preserve original JavaScript Error identity');
        callbackMode = 'normal';
        const recovered: RoundTrip = probe.roundTrip(this.token, 1, 'recovery', bytes([2]));
        equal(recovered.total, 11, 'main session works after callback exception');
      });

      this.record('open while owner session is live is rejected as Busy', () => {
        expectThrows(() => probe.open(() => 0), 'Busy', 'second live session');
      });

      this.record('close from inside callback is rejected as Busy', () => {
        callbackMode = 'close-busy';
        const value: RoundTrip = probe.roundTrip(this.token, 1, 'busy', bytes([3]));
        assert(busySeen, 'close during callback must report Busy');
        equal(value.callbackResult, 12, 'callback return after rejected close');
        callbackMode = 'normal';
      });

      probe.close(this.token);
      this.token = 0;
      let edgeText: string = '';
      this.token = probe.open(this.callbackFor(0, (_kind, _value, text, data) => {
        equal(text, edgeText, 'callback UTF-16 code units');
        equal(JSON.stringify(byteValues(data)), '[0,128,255]', 'callback binary bytes');
        return 19;
      }));
      this.record('UTF-16 boundaries and binary data survive both directions', () => {
        const cases: string[] = ['', '\u0000', '漢字', '🙂', '\ud800', '\udc00', 'a\u0000\ud800z'];
        for (const text of cases) {
          edgeText = text;
          const result: RoundTrip = probe.roundTrip(this.token, 0, text, bytes([0, 128, 255]));
          equal(result.text, text, 'returned UTF-16 code units');
          equal(result.callbackResult, 19, 'edge callback return');
          equal(JSON.stringify(byteValues(result.bytes)), '[0,128,255]', 'returned binary bytes');
        }
      });
      const edgeToken: number = this.token;
      probe.close(edgeToken);
      this.token = 0;
      this.record('closed token rejects new managed calls', () => {
        expectThrows(() => probe.roundTrip(edgeToken, 1, 'closed', bytes([])), 'Closed', 'closed token');
      });

      let workerCalls: number = 0;
      this.token = probe.open(this.callbackFor(1, (_kind, total, text, data) => {
        workerCalls += 1;
        equal(total, workerCalls * (workerCalls + 1) / 2, 'ordered worker managed total');
        equal(text, 'worker', 'worker payload text');
        equal(JSON.stringify(byteValues(data)), JSON.stringify([workerCalls]), 'worker sequence bytes');
        return 0;
      }));
      const afterWorker: Snapshot = await this.runWorkerBatch(this.token, 8);
      equal(afterWorker.total, 36, 'worker managed sum');
      equal(afterWorker.managedEntries, 8, 'wrong-thread gate adds no managed entry');
      equal(workerCalls, 8, 'worker callbacks delivered in order');
      assert(afterWorker.wrongThreadRejected, 'wrong-thread probe rejected before managed entry');
      equal(afterWorker.callbackThread, afterWorker.ownerThread, 'TSFN executes on runtime owner');
      equal(afterWorker.callbackErrors, 0, 'worker callback errors');
      this.resultsRef.push({ name: 'wrong-thread gate and worker callbacks', passed: true, detail: JSON.stringify(afterWorker) });
      this.results = this.resultsRef.slice();

      this.record('deterministic bounded queue rejects caller-owned payloads', () => {
        probe.queueBurst(this.token, 64);
        const state: Snapshot = probe.snapshot(this.token);
        equal(state.accepted, 40, 'eight delivered plus thirty-two queued');
        equal(state.queueFull, 32, 'queue full payload count');
        equal(state.freed, 40, 'eight delivered plus thirty-two rejected payloads freed');
        equal(state.total, 36, 'queued payloads cannot enter managed runtime synchronously');
      });
      const oldToken: number = this.token;
      probe.close(oldToken);
      probe.close(oldToken);
      this.token = 0;
      const closed: Snapshot = await waitForSnapshot(oldToken, (state) => state.finalized && state.allocated === state.freed, 'queued close');
      this.record('close discards queued generation and remains idempotent', () => {
        assert(closed.closed, 'session marked closed');
        equal(closed.total, 36, 'closed session cached total');
        equal(closed.managedEntries, 8, 'close never dispatches stale tasks');
        equal(closed.discarded, 32, 'queued payloads discarded on close');
        equal(closed.allocated, closed.freed, 'all payload ownership released');
      });

      this.token = probe.open((_kind, _value, _text, _data) => 0);
      const heldToken: number = this.token;
      probe.holdWorker(heldToken);
      assert(probe.snapshot(heldToken).workerRunning, 'held producer remains active until stop');
      probe.close(heldToken);
      this.token = 0;
      const held: Snapshot = await waitForSnapshot(heldToken, (state) => state.finalized && state.allocated === state.freed, 'active producer close');
      this.record('active producer stops without waiting for JS', () => {
        equal(held.discarded, 1, 'held payload discarded');
        equal(held.total, 0, 'stale payload did not enter managed runtime');
        assert(!held.workerRunning, 'producer joined');
      });

      this.record('closed owner can reopen and execute managed call', () => {
        this.token = probe.open((_kind, _value, _text, _data) => 5);
        const result: RoundTrip = probe.roundTrip(this.token, 3, 'reopen', bytes([4]));
        equal(result.total, 3, 'reopened owner session total');
        equal(result.callbackResult, 5, 'reopened callback result');
        equal(probe.snapshot(this.token).managedEntries, 1, 'reopened managed entry');
        probe.close(this.token);
        this.token = 0;
      });
      await new Promise<void>((resolve) => setTimeout(resolve, 25));

      const diagnostics: Diagnostics = probe.diagnostics();
      assert(diagnostics.liveSessions === 0, `no leaked native sessions: ${JSON.stringify(diagnostics)}`);
      report.scenarios = this.resultsRef;
      report.diagnostics = diagnostics;
      this.status = `PASS · ${this.resultsRef.length} scenarios · native diagnostics ${JSON.stringify(diagnostics)}`;
      this.logResult(true, report);
    } catch (error) {
      failed = String(error);
      if (this.token !== 0) {
        try {
          probe.close(this.token);
        } catch (closeError) {
          failed += `; cleanup: ${String(closeError)}`;
        }
        this.token = 0;
      }
      report.scenarios = this.resultsRef;
      report.failure = failed;
      try {
        report.diagnostics = probe.diagnostics();
      } catch (diagnosticError) {
        report.diagnosticsFailure = String(diagnosticError);
      }
      this.status = `FAIL · ${failed}`;
      this.logResult(false, report);
    } finally {
      this.busy = false;
    }
  }

  private beginCleanupMode(): void {
    this.status = 'Cleanup mode: Worker owns all managed runtime calls; UI stays native-diagnostics-only.';
    this.results = [{ name: 'cleanup mode entered', passed: true, detail: `runId=${this.runId}` }];
    const cleanupWorker: worker.ThreadWorker = new worker.ThreadWorker('entry/ets/workers/CleanupWorker.ets');
    this.cleanupWorker = cleanupWorker;
    cleanupWorker.onmessage = (event: MessageEvents) => {
      try {
        const message = event.data as WorkerReply;
        if (message.kind === 'ready') {
          const snapshot = message.snapshot as Snapshot;
          assert(snapshot.workerRunning, 'cleanup producer must remain parked and active');
          equal(snapshot.accepted, 32, 'cleanup queue capacity');
          equal(snapshot.queueFull, 33, 'burst and held producer rejected payloads');
          this.cleanupStatus = `Worker owner ready: ${JSON.stringify(snapshot)}`;
          hilog.info(DOMAIN, TAG, 'cleanup worker ready: %{public}s', JSON.stringify(snapshot));
          this.verifyCleanupAfterWorkerTerminate(cleanupWorker);
        } else if (message.kind === 'error') {
          this.failCleanup(String(message.error));
        }
      } catch (error) {
        this.failCleanup(String(error));
      }
    };
    cleanupWorker.onerror = (event: ErrorEvent) => {
      this.failCleanup(`Worker error ${event.message}`);
    };
    try {
      const command: WorkerCommand = { command: 'start' };
      cleanupWorker.postMessage(command);
    } catch (error) {
      this.failCleanup(String(error));
    }
  }

  private async verifyCleanupAfterWorkerTerminate(cleanupWorker: worker.ThreadWorker): Promise<void> {
    try {
      cleanupWorker.terminate();
      this.cleanupWorker = undefined;
      const started: number = Date.now();
      let diagnostics: Diagnostics = probe.diagnostics();
      while ((diagnostics.liveSessions !== 0 || diagnostics.livePayloads !== 0 || !diagnostics.ownerTerminated) && Date.now() - started < TIMEOUT_MS) {
        await new Promise<void>((resolve) => setTimeout(resolve, 25));
        diagnostics = probe.diagnostics();
      }
      assert(diagnostics.liveSessions === 0, `cleanup leaves zero sessions: ${JSON.stringify(diagnostics)}`);
      assert(diagnostics.livePayloads === 0, `cleanup leaves zero queued payloads: ${JSON.stringify(diagnostics)}`);
      assert(diagnostics.cleanupHooks >= 1, `environment cleanup hook observed: ${JSON.stringify(diagnostics)}`);
      assert(diagnostics.ownerTerminated, `owner thread terminated: ${JSON.stringify(diagnostics)}`);
      const report: ScenarioReport = { mode: 'cleanup', runId: this.runId, diagnostics: diagnostics, worker: this.cleanupStatus };
      this.status = `PASS · worker termination cleanup · ${JSON.stringify(diagnostics)}`;
      this.results = [{ name: 'Worker owner termination cleanup', passed: true, detail: JSON.stringify(diagnostics) }];
      this.logResult(true, report);
    } catch (error) {
      this.failCleanup(String(error));
    }
  }

  private failCleanup(message: string): void {
    const report: ScenarioReport = { mode: 'cleanup', runId: this.runId, failure: message };
    this.cleanupStatus = message;
    this.status = `FAIL · ${message}`;
    this.results = [{ name: 'Worker owner termination cleanup', passed: false, detail: message }];
    this.logResult(false, report);
  }

  build() {
    Scroll() {
      Column({ space: 16 }) {
        Row({ space: 12 }) {
          Column() {
            Text('AJNI / HARMONYOS').fontSize(13).fontWeight(FontWeight.Bold).fontColor('#E57D51');
            Text('Native Runtime Validation').fontSize(25).fontWeight(FontWeight.Bold).fontColor('#F4E8CF');
          }.alignItems(HorizontalAlign.Start).layoutWeight(1);
          Text(this.mode.toUpperCase()).fontSize(12).fontWeight(FontWeight.Bold).fontColor('#B6D7C2').backgroundColor('#21483D').padding({ left: 12, right: 12, top: 8, bottom: 8 }).borderRadius(4);
        }.width('100%');
        Text(`API 26 · x86_64 runtime · run ${this.runId || 'manual'}`).fontSize(13).fontColor('#B6C5BE').width('100%');
        Text(this.status).fontSize(15).fontColor(this.status.startsWith('FAIL') ? '#FFB4A9' : '#F4E8CF').width('100%').padding(16).backgroundColor('#1B2925').borderRadius(4);
        if (this.mode !== 'cleanup') {
          Button(this.busy ? 'Running native scenarios…' : 'Run native scenarios')
            .width('100%').height(52).enabled(!this.busy)
            .backgroundColor('#347A59').fontColor('#FFFFFF')
            .onClick(() => { this.runSuite(); });
          Button('Run Worker cleanup check')
            .width('100%').height(48).enabled(!this.busy)
            .backgroundColor('#31423B').fontColor('#F4E8CF')
            .onClick(() => { this.mode = 'cleanup'; this.beginCleanupMode(); });
        } else {
          Text(this.cleanupStatus).fontSize(13).fontColor('#B6C5BE').width('100%');
          Button('Restart cleanup Worker').width('100%').height(48).backgroundColor('#31423B').fontColor('#F4E8CF').onClick(() => { this.beginCleanupMode(); });
        }
        Row({ space: 8 }) {
          Text('SCENARIO').fontSize(11).fontWeight(FontWeight.Bold).fontColor('#9AB8AA').layoutWeight(1);
          Text('RESULT').fontSize(11).fontWeight(FontWeight.Bold).fontColor('#9AB8AA');
        }.width('100%').padding({ top: 8, bottom: 8 });
        ForEach(this.results, (item: ScenarioResult) => {
          Column({ space: 6 }) {
            Row() {
              Text(item.name).fontSize(14).fontWeight(FontWeight.Medium).fontColor('#F4E8CF').layoutWeight(1);
              Text(item.passed ? 'PASS' : 'FAIL').fontSize(12).fontWeight(FontWeight.Bold).fontColor(item.passed ? '#79D49E' : '#FF8E83');
            }.width('100%');
            Text(item.detail).fontSize(12).fontColor('#B6C5BE').width('100%');
          }.width('100%').padding(12).backgroundColor('#1B2925').borderRadius(4);
        }, (item: ScenarioResult) => item.name);
        Text('Lifecycle: background/foreground keeps the owner session open. Destroying the UI stage closes only a UI-owned suite session; cleanup mode exercises environment teardown by terminating its Worker owner.')
          .fontSize(12).fontColor('#9AB8AA').width('100%').padding({ top: 8, bottom: 16 });
      }
      .padding({ left: 20, right: 20, top: 24, bottom: 24 })
      .width('100%')
      .alignItems(HorizontalAlign.Start);
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#12332D');
  }
}
