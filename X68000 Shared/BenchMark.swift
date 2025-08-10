import Foundation
import QuartzCore

class Benchmark {

  // 開始時刻を保存する変数 - 高精度モノトニッククロック使用
  private let startTime: CFTimeInterval
  private let key: String

  // 処理開始
  init(_ key: String) {
    self.startTime = CACurrentMediaTime()  // 高精度モノトニッククロック
    self.key = key
  }

  // 処理終了
  func finish() {
    let elapsed = CACurrentMediaTime() - self.startTime
    // String(format:) を避けて高速化、単純な乗算と丸めで十分
    let elapsedMs = (elapsed * 100000.0).rounded() / 100.0  // 小数点以下3桁の精度
    debugLog("[\(key)]: \(elapsedMs)ms", category: .emulation)
  }

  // 処理をブロックで受け取る
  class func measure(_ key: String, block: () -> ()) {
    let benchmark = Benchmark(key)
    block()
    benchmark.finish()
  }
  
  // より高精度が必要な場合のDispatchTime版
  class func measureHighPrecision(_ key: String, block: () -> ()) {
    let startTime = DispatchTime.now()
    block()
    let endTime = DispatchTime.now()
    let elapsed = Double(endTime.uptimeNanoseconds - startTime.uptimeNanoseconds) / 1_000_000.0  // ns to ms
    let elapsedRounded = (elapsed * 1000.0).rounded() / 1000.0  // 小数点以下3桁の精度
    debugLog("[\(key)]: \(elapsedRounded)ms", category: .emulation)
  }
}
