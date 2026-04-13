//
//  X68ErrorHandling.swift
//  X68000 Shared
//
//  Created by Improvement Phase 1
//  Copyright 2025 Awed. All rights reserved.
//

import Foundation
#if os(macOS)
import AppKit
#elseif os(iOS)
import UIKit
#endif

/// X68Mac専用のエラー定義
enum X68MacError: LocalizedError {
    case romFileNotFound(String)
    case diskImageCorrupted(String)
    case insufficientMemory
    case unsupportedFileFormat(String)
    case fileAccessDenied(String)
    case fileTooLarge(String, Int)
    case invalidConfiguration(String)
    case emulationError(String)
    case audioInitializationFailed
    case networkError(String)
    
    var errorDescription: String? {
        switch self {
        case .romFileNotFound(let filename):
            return "ROMファイル '\(filename)' が見つかりません。CGROM.DATとIPLROM.DATをDocumentsフォルダに配置してください。"
        case .diskImageCorrupted(let filename):
            return "ディスクイメージ '\(filename)' が破損しています。別のファイルを試してください。"
        case .insufficientMemory:
            return "メモリが不足しています。他のアプリケーションを終了してから再試行してください。"
        case .unsupportedFileFormat(let format):
            return "サポートされていないファイル形式 '\(format)' です。.dim、.xdf、.hdfファイルを使用してください。"
        case .fileAccessDenied(let filename):
            return "ファイル '\(filename)' へのアクセスが拒否されました。ファイルの権限を確認してください。"
        case .fileTooLarge(let filename, let size):
            return "ファイル '\(filename)' が大きすぎます（\(size)MB）。より小さなファイルを使用してください。"
        case .invalidConfiguration(let detail):
            return "設定が無効です: \(detail)"
        case .emulationError(let detail):
            return "エミュレーションエラー: \(detail)"
        case .audioInitializationFailed:
            return "オーディオシステムの初期化に失敗しました。"
        case .networkError(let detail):
            return "ネットワークエラー: \(detail)"
        }
    }
    
    var recoverySuggestion: String? {
        switch self {
        case .romFileNotFound:
            return "X68000の実機からROMファイルを取得し、適切な場所に配置してください。"
        case .diskImageCorrupted:
            return "別のディスクイメージファイルを試すか、元のファイルを再取得してください。"
        case .insufficientMemory:
            return "不要なアプリケーションを終了してメモリを解放してください。"
        case .unsupportedFileFormat:
            return "対応しているファイル形式（.dim、.xdf、.hdf）を使用してください。"
        case .fileAccessDenied:
            return "ファイルの権限設定を確認し、必要に応じて管理者権限で実行してください。"
        case .fileTooLarge:
            return "より小さなファイルを使用するか、ファイルを分割してください。"
        default:
            return nil
        }
    }
}

/// エラーハンドリングプロトコル
protocol X68ErrorHandling {
    func handleError(_ error: Error, context: String)
    func showUserError(_ message: String, title: String?)
    func showUserWarning(_ message: String, title: String?)
}

/// エラーハンドリングの実装
extension X68ErrorHandling {
    func handleError(_ error: Error, context: String = "") {
        let contextInfo = context.isEmpty ? "" : " (\(context))"
        errorLog("Error occurred\(contextInfo)", error: error, category: .x68mac)
        
        // ユーザーフレンドリーなメッセージを表示
        let userMessage = (error as? X68MacError)?.errorDescription ?? error.localizedDescription
        showUserError(userMessage, title: "エラー")
    }
    
    func handleWarning(_ message: String, context: String = "") {
        let contextInfo = context.isEmpty ? "" : " (\(context))"
        warningLog("Warning\(contextInfo): \(message)", category: .x68mac)
        showUserWarning(message, title: "警告")
    }
}

#if os(macOS)
extension NSViewController: X68ErrorHandling {
    func showUserError(_ message: String, title: String? = nil) {
        DispatchQueue.main.async {
            let alert = NSAlert()
            alert.messageText = title ?? "エラー"
            alert.informativeText = message
            alert.alertStyle = .critical
            alert.addButton(withTitle: "OK")
            
            if let window = self.view.window {
                alert.beginSheetModal(for: window) { _ in }
            } else {
                alert.runModal()
            }
        }
    }
    
    func showUserWarning(_ message: String, title: String? = nil) {
        DispatchQueue.main.async {
            let alert = NSAlert()
            alert.messageText = title ?? "警告"
            alert.informativeText = message
            alert.alertStyle = .warning
            alert.addButton(withTitle: "OK")
            
            if let window = self.view.window {
                alert.beginSheetModal(for: window) { _ in }
            } else {
                alert.runModal()
            }
        }
    }
}
#endif

#if os(iOS)
extension UIViewController: X68ErrorHandling {
    func showUserError(_ message: String, title: String? = nil) {
        DispatchQueue.main.async {
            let alert = UIAlertController(
                title: title ?? "エラー",
                message: message,
                preferredStyle: .alert
            )
            alert.addAction(UIAlertAction(title: "OK", style: .default))
            self.present(alert, animated: true)
        }
    }
    
    func showUserWarning(_ message: String, title: String? = nil) {
        DispatchQueue.main.async {
            let alert = UIAlertController(
                title: title ?? "警告",
                message: message,
                preferredStyle: .alert
            )
            alert.addAction(UIAlertAction(title: "OK", style: .default))
            self.present(alert, animated: true)
        }
    }
}
#endif

/// 安全なオプショナル処理のためのヘルパー関数
func safeUnwrap<T>(_ optional: T?, errorMessage: String, context: String = "") throws -> T {
    guard let value = optional else {
        let error = X68MacError.invalidConfiguration("\(errorMessage)\(context.isEmpty ? "" : " (\(context))")")
        throw error
    }
    return value
}

/// 安全なファイル操作のためのヘルパー関数
func safeFileOperation<T>(_ operation: () throws -> T, filename: String) throws -> T {
    do {
        return try operation()
    } catch let error as NSError {
        switch error.code {
        case NSFileReadNoSuchFileError:
            throw X68MacError.romFileNotFound(filename)
        case NSFileReadNoPermissionError:
            throw X68MacError.fileAccessDenied(filename)
        default:
            throw error
        }
    }
}

