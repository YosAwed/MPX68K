//
//  X68Security.swift
//  X68000 Shared
//
//  Created by Improvement Phase 1
//  Copyright 2025 GOROman. All rights reserved.
//

import Foundation
import UniformTypeIdentifiers
import CryptoKit

/// ファイルセキュリティ管理クラス
class X68Security {
    
    /// サポートされているファイル拡張子
    static let supportedExtensions = ["dim", "xdf", "d88", "hdm", "hdf", "dat"]
    
    /// 最大ファイルサイズ（100MB）
    static let maxFileSize: Int64 = 100 * 1024 * 1024
    
    /// ROMファイルの期待サイズ
    static let expectedROMSizes: [String: Int] = [
        "CGROM.DAT": 768 * 1024,    // 768KB
        "IPLROM.DAT": 128 * 1024    // 128KB
    ]
    
    /// ファイル形式の検証
    /// - Parameter url: 検証するファイルのURL
    /// - Returns: 有効なファイル形式の場合true
    static func isValidFileFormat(_ url: URL) -> Bool {
        let fileExtension = url.pathExtension.lowercased()
        let isSupported = supportedExtensions.contains(fileExtension)
        
        debugLog("File format validation: \(url.lastPathComponent) -> \(isSupported)", category: .fileSystem)
        return isSupported
    }
    
    /// ファイルサイズの検証
    /// - Parameter url: 検証するファイルのURL
    /// - Returns: 適切なサイズの場合true
    /// - Throws: ファイルアクセスエラー
    static func isValidFileSize(_ url: URL) throws -> Bool {
        let fileSize = try getFileSize(url)
        let isValid = fileSize <= maxFileSize
        
        if !isValid {
            let sizeMB = fileSize / (1024 * 1024)
            warningLog("File too large: \(url.lastPathComponent) (\(sizeMB)MB)", category: .fileSystem)
        }
        
        return isValid
    }
    
    /// ファイルサイズを取得
    /// - Parameter url: ファイルのURL
    /// - Returns: ファイルサイズ（バイト）
    /// - Throws: ファイルアクセスエラー
    static func getFileSize(_ url: URL) throws -> Int64 {
        let attributes = try FileManager.default.attributesOfItem(atPath: url.path)
        guard let fileSize = attributes[.size] as? Int64 else {
            throw X68MacError.invalidConfiguration("Could not determine file size for \(url.lastPathComponent)")
        }
        return fileSize
    }
    
    /// ROMファイルの検証
    /// - Parameter url: ROMファイルのURL
    /// - Returns: 有効なROMファイルの場合true
    /// - Throws: 検証エラー
    static func validateROMFile(_ url: URL) throws -> Bool {
        let filename = url.lastPathComponent.uppercased()
        
        // ファイル名の検証
        guard expectedROMSizes.keys.contains(filename) else {
            throw X68MacError.unsupportedFileFormat("Invalid ROM filename: \(filename)")
        }
        
        // ファイルサイズの検証
        let fileSize = try getFileSize(url)
        let expectedSize = expectedROMSizes[filename]!
        
        guard fileSize == expectedSize else {
            throw X68MacError.diskImageCorrupted("ROM file size mismatch: expected \(expectedSize), got \(fileSize)")
        }
        
        // ファイルの読み取り可能性を確認
        guard FileManager.default.isReadableFile(atPath: url.path) else {
            throw X68MacError.fileAccessDenied(filename)
        }
        
        infoLog("ROM file validated: \(filename)", category: .fileSystem)
        return true
    }
    
    /// ディスクイメージファイルの検証
    /// - Parameter url: ディスクイメージファイルのURL
    /// - Returns: 有効なディスクイメージの場合true
    /// - Throws: 検証エラー
    static func validateDiskImage(_ url: URL) throws -> Bool {
        // ファイル形式の検証
        guard isValidFileFormat(url) else {
            throw X68MacError.unsupportedFileFormat(url.pathExtension)
        }
        
        // ファイルサイズの検証
        guard try isValidFileSize(url) else {
            let fileSize = try getFileSize(url)
            let sizeMB = Int(fileSize / (1024 * 1024))
            throw X68MacError.fileTooLarge(url.lastPathComponent, sizeMB)
        }
        
        // ファイルの読み取り可能性を確認
        guard FileManager.default.isReadableFile(atPath: url.path) else {
            throw X68MacError.fileAccessDenied(url.lastPathComponent)
        }
        
        // 基本的なファイル内容の検証
        try validateFileContent(url)
        
        infoLog("Disk image validated: \(url.lastPathComponent)", category: .fileSystem)
        return true
    }
    
    /// ファイル内容の基本検証
    /// - Parameter url: 検証するファイルのURL
    /// - Throws: 内容が無効な場合のエラー
    private static func validateFileContent(_ url: URL) throws {
        let fileHandle = try FileHandle(forReadingFrom: url)
        defer { fileHandle.closeFile() }
        
        // ファイルの最初の数バイトを読み取って基本的な検証
        let headerData = fileHandle.readData(ofLength: 16)
        
        // 空ファイルのチェック
        guard !headerData.isEmpty else {
            throw X68MacError.diskImageCorrupted("File is empty: \(url.lastPathComponent)")
        }
        
        // ファイル拡張子に応じた基本的な検証
        let fileExtension = url.pathExtension.lowercased()
        switch fileExtension {
        case "dim":
            try validateDIMFormat(headerData, filename: url.lastPathComponent)
        case "xdf":
            try validateXDFFormat(headerData, filename: url.lastPathComponent)
        case "d88":
            try validateD88Format(headerData, filename: url.lastPathComponent)
        default:
            // その他の形式は基本的な検証のみ
            break
        }
    }
    
    /// DIMファイル形式の検証
    private static func validateDIMFormat(_ headerData: Data, filename: String) throws {
        // DIMファイルの基本的な検証（実際の仕様に基づいて実装）
        guard headerData.count >= 16 else {
            throw X68MacError.diskImageCorrupted("Invalid DIM header: \(filename)")
        }
        // 追加の検証ロジックをここに実装
    }
    
    /// XDFファイル形式の検証
    private static func validateXDFFormat(_ headerData: Data, filename: String) throws {
        // XDFファイルの基本的な検証
        guard headerData.count >= 16 else {
            throw X68MacError.diskImageCorrupted("Invalid XDF header: \(filename)")
        }
        // 追加の検証ロジックをここに実装
    }
    
    /// D88ファイル形式の検証
    private static func validateD88Format(_ headerData: Data, filename: String) throws {
        // D88ファイルの基本的な検証
        guard headerData.count >= 16 else {
            throw X68MacError.diskImageCorrupted("Invalid D88 header: \(filename)")
        }
        // 追加の検証ロジックをここに実装
    }
    
    /// セキュリティスコープ付きファイルアクセス
    /// - Parameters:
    ///   - url: アクセスするファイルのURL
    ///   - operation: 実行する操作
    /// - Returns: 操作の結果
    /// - Throws: アクセスエラーまたは操作エラー
    static func secureFileAccess<T>(_ url: URL, operation: () throws -> T) throws -> T {
        guard url.startAccessingSecurityScopedResource() else {
            throw X68MacError.fileAccessDenied(url.lastPathComponent)
        }
        
        defer {
            url.stopAccessingSecurityScopedResource()
        }
        
        return try operation()
    }
    
    /// ファイルのチェックサム計算
    /// - Parameter url: チェックサムを計算するファイルのURL
    /// - Returns: SHA256チェックサム
    /// - Throws: ファイルアクセスエラー
    static func calculateChecksum(_ url: URL) throws -> String {
        return try secureFileAccess(url) {
            let data = try Data(contentsOf: url)
            let hash = SHA256.hash(data: data)
            return hash.compactMap { String(format: "%02x", $0) }.joined()
        }
    }
    
    /// 安全なファイル読み込み
    /// - Parameter url: 読み込むファイルのURL
    /// - Returns: ファイルデータ
    /// - Throws: 検証エラーまたは読み込みエラー
    static func safeLoadFile(_ url: URL) throws -> Data {
        // ファイル検証
        try validateDiskImage(url)
        
        // セキュリティスコープ付きでファイル読み込み
        return try secureFileAccess(url) {
            return try Data(contentsOf: url)
        }
    }
}

