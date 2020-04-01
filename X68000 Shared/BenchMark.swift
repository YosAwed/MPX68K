import Foundation

class Benchmark {

  // 開始時刻を保存する変数
  var startTime: NSDate
  var key: String

  // 処理開始
  init(_ key: String) {
    self.startTime = NSDate()
    self.key = key
  }

  // 処理終了
  func finish() {
    let elapsed = NSDate().timeIntervalSince(self.startTime as Date) as Double
    let formatedElapsed = String(format: "%2.5f", elapsed*1000)
    print("[\(key)]: \(formatedElapsed)ms")
  }

  // 処理をブロックで受け取る
  class func measure(_ key: String, block: () -> ()) {
    let benchmark = Benchmark(key)
    block()
    benchmark.finish()
  }
}
