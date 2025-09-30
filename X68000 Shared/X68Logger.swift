//
//  X68Logger.swift
//  X68000 Shared
//
//  Created by Improvement Phase 1
//  Copyright 2025 Awed. All rights reserved.
//

import Foundation
import os.log

/// X68Mac専用のログシステム
/// 統一されたログ出力とカテゴリ分けを提供
extension Logger {
    /// メインアプリケーションログ
    static let x68mac = Logger(subsystem: "com.goroman.x68mac", category: "main")
    
    /// ファイルシステム関連ログ
    static let fileSystem = Logger(subsystem: "com.goroman.x68mac", category: "filesystem")
    
    /// エミュレーション関連ログ
    static let emulation = Logger(subsystem: "com.goroman.x68mac", category: "emulation")
    
    /// オーディオシステムログ
    static let audio = Logger(subsystem: "com.goroman.x68mac", category: "audio")
    
    /// 入力システムログ
    static let input = Logger(subsystem: "com.goroman.x68mac", category: "input")
    
    /// UI関連ログ
    static let ui = Logger(subsystem: "com.goroman.x68mac", category: "ui")
    
    /// ネットワーク関連ログ
    static let network = Logger(subsystem: "com.goroman.x68mac", category: "network")
}

/// ランタイムでログ出力量を制御する設定
struct X68LogConfig {
    /// infoLog 有効化（既定: false で抑制）
    static var enableInfoLogs: Bool = false
    /// debugLog 有効化（既定: false で抑制）
    static var enableDebugLogs: Bool = false
}

/// デバッグ専用のログ関数
/// リリースビルドでは出力されない
func debugLog(_ message: String, category: Logger = .x68mac) {
    #if DEBUG
    if X68LogConfig.enableDebugLogs {
        category.debug("\(message)")
    }
    #endif
}

/// 情報ログ関数
func infoLog(_ message: String, category: Logger = .x68mac) {
    if X68LogConfig.enableInfoLogs {
        category.info("\(message)")
    }
}

/// 警告ログ関数
func warningLog(_ message: String, category: Logger = .x68mac) {
    category.warning("\(message)")
}

/// エラーログ関数
func errorLog(_ message: String, error: Error? = nil, category: Logger = .x68mac) {
    if let error = error {
        category.error("\(message): \(error.localizedDescription)")
    } else {
        category.error("\(message)")
    }
}

/// 重要なログ関数（常に出力される）
func criticalLog(_ message: String, category: Logger = .x68mac) {
    category.critical("\(message)")
}

/// パフォーマンス測定用のログ関数
func performanceLog<T>(_ operation: String, category: Logger = .x68mac, block: () throws -> T) rethrows -> T {
    let startTime = CFAbsoluteTimeGetCurrent()
    let result = try block()
    let timeElapsed = CFAbsoluteTimeGetCurrent() - startTime
    
    #if DEBUG
        category.debug("Performance: \(operation) took \(String(format: "%.3f", timeElapsed))s")
    #endif
    
    return result
}
