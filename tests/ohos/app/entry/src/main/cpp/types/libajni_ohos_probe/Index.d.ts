export interface RoundTrip {
  total: number;
  callbackResult: number;
  text: string;
  bytes: ArrayBuffer;
}

export interface Snapshot {
  token: number;
  total: number;
  managedEntries: number;
  accepted: number;
  delivered: number;
  discarded: number;
  allocated: number;
  freed: number;
  queueFull: number;
  workerRunning: boolean;
  closed: boolean;
  ownerThread: string;
  callbackThread: string;
  wrongThreadRejected: boolean;
  callbackErrors: number;
  finalized: boolean;
}

export interface Diagnostics {
  liveSessions: number;
  livePayloads: number;
  finalizedSessions: number;
  cleanupHooks: number;
  ownerInitialized: boolean;
  ownerTerminated: boolean;
}

export type Callback = (kind: number, value: number, text: string, bytes: ArrayBuffer) => number;

export const open: (callback: Callback) => number;
export const roundTrip: (token: number, delta: number, text: string, bytes: ArrayBuffer) => RoundTrip;
export const startWorker: (token: number, count: number) => void;
export const queueBurst: (token: number, count: number) => void;
export const close: (token: number) => void;
export const snapshot: (token: number) => Snapshot;
export const diagnostics: () => Diagnostics;
export const holdWorker: (token: number) => void;
