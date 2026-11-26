// SPDX-License-Identifier: GPL-2.0-only
import Foundation
import Vision

let oRequest = VNRecognizeTextRequest()
oRequest.recognitionLanguages = ["zh-Hans", "en-US"]
oRequest.recognitionLevel = .accurate

let strScreenshot = CommandLine.arguments[1]
let oHandler = VNImageRequestHandler(url: URL(fileURLWithPath: strScreenshot))
try oHandler.perform([oRequest])

let vTexts = oRequest.results?.flatMap(
{
    $0.topCandidates(3).map(
    {
        $0.string.replacingOccurrences(of: " ", with: "")
    })
}) ?? []
let vNavigationLabels = ["书架", "最近更新", "下载缓存", "我的"]
let vRecognizedLabels = vNavigationLabels.filter(
{ strLabel in
    vTexts.contains(where:
    {
        $0.contains(strLabel)
    })
})
guard vRecognizedLabels.count >= 2 else
{
    FileHandle.standardError.write(Data("Reader navigation was not visible in the simulator screenshot.\n".utf8))
    exit(EXIT_FAILURE)
}
print("Visible reader navigation: \(vRecognizedLabels.joined(separator: ", "))")
