// SPDX-License-Identifier: GPL-2.0-only
export interface NativeNovel {
  id: number;
  title: string;
  author: string;
  status: string;
  intro: string;
  latestChapter: string;
}

export interface NativeChapter {
  id: number;
  title: string;
}

export interface NativeVolume {
  id: number;
  title: string;
  chapters: NativeChapter[];
}

export interface NativeRequest {
  operation: string;
  cacheDir?: string;
  page?: number;
  query?: string;
  id?: number;
  chapterId?: number;
  includeImages?: boolean;
  timeoutMs?: number;
  account?: string;
  url?: string;
}

export interface NativeResult {
  novels?: NativeNovel[];
  pages?: number;
  novel?: NativeNovel;
  volumes?: NativeVolume[];
  text?: string;
  username?: string;
  bytes?: ArrayBuffer;
  chaptersCompleted?: number;
  chaptersTotal?: number;
}

export type BrowserTransport = (url: string, method: string, body: string, responseId: number,
  followRedirects: boolean, asset: boolean) => void;
export type DownloadProgress = (completed: number, total: number) => void;

export function sdkVersion(): string;
export function run(request: NativeRequest, browser?: BrowserTransport,
  progress?: DownloadProgress): Promise<NativeResult>;
export function respond(responseId: number, status: number, body: ArrayBuffer,
  effectiveUrl: string, contentType?: string, location?: string): void;
export function reject(responseId: number): void;
