// SPDX-License-Identifier: GPL-2.0-only
import { bundleManager } from '@kit.AbilityKit';
import { rcp } from '@kit.RemoteCommunicationKit';

const strOrigin: string = 'https://github.com';
const strRepositoryPath: string = '/lucky-ji/wenku8_reader';
export const strRepositoryUrl: string = strOrigin + strRepositoryPath;
const strLatestUrl: string = strRepositoryUrl + '/releases/latest';
const strReleasePath: string = strRepositoryPath + '/releases/tag/';
const strReleasePrefix: string = strOrigin + strReleasePath;
const oVersionPattern: RegExp = /^v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$/;
const nMaxVersionComponent: number = 2147483647;

export enum UpdateState
{
  UpToDate,
  Available,
  InvalidRelease,
  NetworkError
}

export class UpdateResult
{
  readonly nState: UpdateState;
  readonly strVersion: string;
  readonly strUrl: string;

  constructor(nState: UpdateState, strVersion: string = '', strUrl: string = '')
  {
    this.nState = nState;
    this.strVersion = strVersion;
    this.strUrl = strUrl;
  }
}

function parseVersion(strVersion: string): number[] | undefined
{
  const oMatch = oVersionPattern.exec(strVersion);
  if (!oMatch || oMatch[0] !== strVersion)
  {
    return undefined;
  }

  const vParts: number[] = [Number(oMatch[1]), Number(oMatch[2]), Number(oMatch[3])];
  return vParts.some((nPart: number) => nPart > nMaxVersionComponent) ? undefined : vParts;
}

function compareVersions(vLeft: number[], vRight: number[]): number
{
  for (let nPart = 0; nPart < vLeft.length; ++nPart)
  {
    const nOrder = vLeft[nPart] - vRight[nPart];
    if (nOrder !== 0)
    {
      return nOrder;
    }
  }

  return 0;
}

function evaluateRelease(strCurrent: string, strLocation: string): UpdateResult
{
  const strUrl = strLocation.startsWith(strReleasePath) ? strOrigin + strLocation : strLocation;
  if (!strUrl.startsWith(strReleasePrefix))
  {
    return new UpdateResult(UpdateState.InvalidRelease);
  }

  const vLatest = parseVersion(strUrl.slice(strReleasePrefix.length));
  const vCurrent = parseVersion(strCurrent);
  if (!vCurrent || !vLatest)
  {
    return new UpdateResult(UpdateState.InvalidRelease);
  }
  if (compareVersions(vLatest, vCurrent) <= 0)
  {
    return new UpdateResult(UpdateState.UpToDate);
  }
  return new UpdateResult(UpdateState.Available, vLatest.join('.'), strUrl);
}

export function installedAppVersion(): string
{
  try
  {
    const oBundle = bundleManager.getBundleInfoForSelfSync(bundleManager.BundleFlag.GET_BUNDLE_INFO_DEFAULT);
    return oBundle.versionName;
  }
  catch
  {
    return '';
  }
}

export async function checkLatestRelease(strCurrent: string): Promise<UpdateResult>
{
  let oSession: rcp.Session | undefined;
  try
  {
    oSession = rcp.createSession({ requestConfiguration: { transfer: {
      autoRedirect: false, timeout: { connectMs: 5000, transferMs: 10000 }
    } } });
    const oResponse = await oSession.head(strLatestUrl);
    if (oResponse.statusCode >= 400 && oResponse.statusCode !== 404)
    {
      return new UpdateResult(UpdateState.NetworkError);
    }

    const vRedirects: number[] = [301, 302, 303, 307, 308];
    const vLocationKeys = Object.keys(oResponse.headers).filter((strKey: string) => strKey.toLowerCase() === 'location');
    if (!vRedirects.includes(oResponse.statusCode) || vLocationKeys.length !== 1)
    {
      return new UpdateResult(UpdateState.InvalidRelease);
    }
    const oLocation = oResponse.headers[vLocationKeys[0]];
    if (typeof oLocation !== 'string')
    {
      return new UpdateResult(UpdateState.InvalidRelease);
    }
    return evaluateRelease(strCurrent, oLocation);
  }
  catch
  {
    return new UpdateResult(UpdateState.NetworkError);
  }
  finally
  {
    oSession?.close();
  }
}
