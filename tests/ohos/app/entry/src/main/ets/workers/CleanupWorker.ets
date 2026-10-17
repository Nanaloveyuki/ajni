import worker, { ThreadWorkerGlobalScope, MessageEvents, ErrorEvent } from '@ohos.worker';
import probe from 'libajni_ohos_probe.so';
import { WorkerCommand, WorkerReply } from './Protocol';

const workerPort: ThreadWorkerGlobalScope = worker.workerPort;
let token: number = 0;

function post(message: WorkerReply): void {
  try {
    workerPort.postMessage(message);
  } catch (error) {
    throw new Error(`Worker message failed: ${String(error)}`);
  }
}

workerPort.onmessage = (event: MessageEvents): void => {
  const message = event.data as WorkerCommand;
  if (message.command === 'start') {
    try {
      token = probe.open((_kind: number, _value: number, _text: string, _bytes: ArrayBuffer): number => 0);
      probe.queueBurst(token, 64);
      probe.holdWorker(token);
      post({ kind: 'ready', snapshot: probe.snapshot(token) });
    } catch (error) {
      post({ kind: 'error', error: String(error) });
    }
  }
};

workerPort.onmessageerror = (_event: MessageEvents): void => {
  post({ kind: 'error', error: 'worker message deserialization failed' });
};

workerPort.onerror = (_event: ErrorEvent): void => {
  post({ kind: 'error', error: 'worker execution failed' });
};
