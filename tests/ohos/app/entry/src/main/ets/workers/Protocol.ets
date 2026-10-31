import { Snapshot, Diagnostics } from 'libajni_ohos_probe.so';

export interface WorkerCommand {
  command: string;
}

export interface WorkerReply {
  kind: string;
  snapshot?: Snapshot;
  diagnostics?: Diagnostics;
  error?: string;
}
