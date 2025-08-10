//
//  FileSystem.swift
//  X68000
//
//  Created by GOROman on 2020/03/31.
//  Copyright 2020 GOROman. All rights reserved.
//

import Foundation
import UniformTypeIdentifiers

// Notification for disk image loading
extension Notification.Name {
    static let diskImageLoaded = Notification.Name("diskImageLoaded")
}

class FileSystem {
    weak var gameScene: GameScene?
    
    // Track currently loading disk pairs to prevent duplicate operations (static to work across instances)
    private static var currentlyLoadingPair: String?
    private static let loadingPairLock = NSLock()
    
    // File search cache for performance optimization
    private static var fileSearchCache: [String: URL] = [:]
    private static var cacheTimestamp: Date = Date.distantPast
    private static let cacheValidityDuration: TimeInterval = 300 // 5 minutes
    private static let cacheAccessLock = NSLock()
    
    init() {
#if false
        let documentsPath = NSSearchPathForDirectoriesInDomains(.documentDirectory, .userDomainMask, true)[0]
        //      let documentsPath = NSHomeDirectory() + "/Documents"
        let libraryPath = NSHomeDirectory() + "/Library"
        let applicationSupportPath = NSHomeDirectory() + "/Library/Application Support"
        let cachesPath = NSHomeDirectory() + "/Library/Caches"
        //        let tmpDirectory = NSHomeDirectory() + "/tmp"
        let tmpDirectory = NSTemporaryDirectory()
#endif
        
        createDocumentsFolder()
        
        
        DispatchQueue.main.async {
            do {
                // for iCloud
                //                let containerURL = FileManager.default.url(forUbiquityContainerIdentifier: nil)
                let containerURL = try? FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
                
                try FileManager.default.startDownloadingUbiquitousItem(at: containerURL!)
            } catch let error as NSError {
                errorLog("Error accessing iCloud container", error: error, category: .fileSystem)
            }
        }
    }
    
    func createDocumentsFolder() {
        // iCloudコンテナのURL
        //        let url = FileManager.default.url(forUbiquityContainerIdentifier: nil)
        let url = try? FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
        let path = (url?.appendingPathComponent("X68000"))!
        do {
            try FileManager.default.createDirectory(at: path, withIntermediateDirectories: true, attributes: nil)
        } catch let error as NSError {
            errorLog("Error creating directory", error: error, category: .fileSystem)
        }
        
#if true
        let fileURL = getDocumentsPath("README.txt")
        let todayText = "POWER TO MAKE YOUR DREAM COME TRUE. Please put CGROM.DAT, IPLROM.DAT, and disk images here.\nSRAM.DAT (save data) will also be saved in this directory."
        if ( FileManager.default.fileExists( atPath: fileURL!.path ) == true ) {
        } else {
            do {
                try todayText.write(to: fileURL!, atomically: true, encoding: .utf8)
            }
            catch {
                errorLog("Write error", category: .fileSystem)
            }
        }
#endif
    }
    
    func getDocumentsPath(_ filename: String )->URL? {
        // for iCloud
        //      let containerURL = FileManager.default.url(forUbiquityContainerIdentifier: nil)
        let containerURL = try? FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
        
        let documentsURL = containerURL?.appendingPathComponent("X68000")
        let url = documentsURL?.appendingPathComponent(filename)
        return url
    }
    
    // Optimized file search with caching and enumeration
    func findFileInDocuments(_ filename: String) -> URL? {
        // Check cache first
        if let cachedResult = getCachedFileLocation(filename) {
            return cachedResult
        }
        
        // Perform enumeration-based search
        return searchFileWithEnumeration(filename)
    }
    
    /// Check if file location is cached and still valid
    private func getCachedFileLocation(_ filename: String) -> URL? {
        return FileSystem.cacheAccessLock.withLock {
            let now = Date()
            let cacheAge = now.timeIntervalSince(FileSystem.cacheTimestamp)
            
            // Check if cache is still valid
            if cacheAge < FileSystem.cacheValidityDuration,
               let cachedURL = FileSystem.fileSearchCache[filename] {
                
                // Verify cached file still exists and is accessible
                if FileManager.default.isReadableFile(atPath: cachedURL.path) {
                    debugLog("Cache hit for \(filename): \(cachedURL.path)", category: .fileSystem)
                    return cachedURL
                } else {
                    // Remove stale entry
                    FileSystem.fileSearchCache.removeValue(forKey: filename)
                }
            }
            
            return nil
        }
    }
    
    /// Perform optimized file search using directory enumeration
    private func searchFileWithEnumeration(_ filename: String) -> URL? {
        guard let containerURL = try? FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: false) else { 
            return nil 
        }
        
        // Get search directories in priority order
        let searchDirectories = getSearchDirectories(containerURL: containerURL)
        
        for searchDir in searchDirectories {
            if let foundURL = searchInDirectory(searchDir, filename: filename) {
                // Cache successful result
                cacheFileLocation(filename, url: foundURL)
                infoLog("Found \(filename) at: \(foundURL.path)", category: .fileSystem)
                return foundURL
            }
        }
        
        debugLog("File \(filename) not found in any search directory", category: .fileSystem)
        return nil
    }
    
    /// Get prioritized list of search directories
    private func getSearchDirectories(containerURL: URL) -> [URL] {
        var directories: [URL] = []
        
        // Priority search directories
        let priorityDirs = [
            containerURL.appendingPathComponent("X68000"),
            containerURL.appendingPathComponent("Documents"), // Legacy compatibility
            containerURL.appendingPathComponent("Inbox"),
            containerURL, // Direct in documents root
            containerURL.appendingPathComponent("Data").appendingPathComponent("Documents").appendingPathComponent("X68000")
        ]
        
        // Add existing directories only
        for dir in priorityDirs {
            var isDirectory: ObjCBool = false
            if FileManager.default.fileExists(atPath: dir.path, isDirectory: &isDirectory) && isDirectory.boolValue {
                directories.append(dir)
            }
        }
        
        // Add user Documents directory if accessible
        if let userHome = FileManager.default.urls(for: .userDirectory, in: .localDomainMask).first {
            let userDocsX68000 = userHome.appendingPathComponent("Documents").appendingPathComponent("X68000")
            var isDirectory: ObjCBool = false
            if FileManager.default.fileExists(atPath: userDocsX68000.path, isDirectory: &isDirectory) && isDirectory.boolValue {
                directories.append(userDocsX68000)
            }
        }
        
        // Add common user Documents location
        let commonUserDocs = URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Documents").appendingPathComponent("X68000")
        var isDirectory: ObjCBool = false
        if FileManager.default.fileExists(atPath: commonUserDocs.path, isDirectory: &isDirectory) && isDirectory.boolValue {
            directories.append(commonUserDocs)
        }
        
        return directories
    }
    
    /// Search for file in specific directory using enumeration
    private func searchInDirectory(_ directory: URL, filename: String) -> URL? {
        let targetURL = directory.appendingPathComponent(filename)
        
        // Direct file check first (most common case)
        if FileManager.default.isReadableFile(atPath: targetURL.path) {
            return targetURL
        }
        
        // Case-insensitive search using enumeration for more thorough search
        let resourceKeys: [URLResourceKey] = [.nameKey, .isRegularFileKey]
        
        guard let enumerator = FileManager.default.enumerator(
            at: directory,
            includingPropertiesForKeys: resourceKeys,
            options: [.skipsHiddenFiles, .skipsSubdirectoryDescendants], // Single level only for performance
            errorHandler: { _, _ in true } // Continue on errors
        ) else {
            return nil
        }
        
        let lowercaseTarget = filename.lowercased()
        
        for case let fileURL as URL in enumerator {
            do {
                let resourceValues = try fileURL.resourceValues(forKeys: Set(resourceKeys))
                
                // Only check regular files
                guard resourceValues.isRegularFile == true,
                      let fileName = resourceValues.name else { continue }
                
                // Case-insensitive comparison for flexibility
                if fileName.lowercased() == lowercaseTarget {
                    if FileManager.default.isReadableFile(atPath: fileURL.path) {
                        debugLog("Found \(filename) via enumeration: \(fileURL.path)", category: .fileSystem)
                        return fileURL
                    }
                }
            } catch {
                continue // Skip files with errors
            }
        }
        
        return nil
    }
    
    /// Cache successful file location
    private func cacheFileLocation(_ filename: String, url: URL) {
        FileSystem.cacheAccessLock.withLock {
            FileSystem.fileSearchCache[filename] = url
            FileSystem.cacheTimestamp = Date()
            debugLog("Cached location for \(filename): \(url.path)", category: .fileSystem)
        }
    }
    
    /// Clear file search cache (useful for testing or when file system changes are detected)
    static func clearFileSearchCache() {
        cacheAccessLock.withLock {
            fileSearchCache.removeAll()
            cacheTimestamp = Date.distantPast
            debugLog("File search cache cleared", category: .fileSystem)
        }
    }
    
    func boot()
    {
        // for iCloud
        //      let containerURL = FileManager.default.url(forUbiquityContainerIdentifier: nil)
        let containerURL = try? FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
        
        // コンテナに追加するフォルダのパス
        if let documentsURL = containerURL?.appendingPathComponent("X68000") {
            scanDiskImagesEfficiently(in: documentsURL)
        }
    }
    
    /// Efficiently scan directory for disk images using FileManager enumerator and URLResourceValues
    private func scanDiskImagesEfficiently(in directory: URL) {
        debugLog("Starting efficient disk image scan in: \(directory.path)", category: .fileSystem)
        
        // Define valid disk extensions as Set for O(1) lookup performance
        let validDiskExtensions: Set<String> = ["dim", "xdf", "d88", "hdm", "hdf"]
        
        // Request only the resource values we need to minimize I/O
        let resourceKeys: [URLResourceKey] = [.nameKey, .isRegularFileKey, .fileResourceTypeKey]
        
        // Use DirectoryEnumerator for stream processing instead of loading entire directory
        guard let enumerator = FileManager.default.enumerator(
            at: directory,
            includingPropertiesForKeys: resourceKeys,
            options: [.skipsHiddenFiles, .skipsPackageDescendants],
            errorHandler: { url, error in
                warningLog("Error accessing file during scan: \(url.path) - \(error.localizedDescription)", category: .fileSystem)
                return true // Continue enumeration
            }
        ) else {
            warningLog("Failed to create directory enumerator for: \(directory.path)", category: .fileSystem)
            return
        }
        
        var scannedCount = 0
        var foundCount = 0
        
        // Stream process files using the enumerator
        for case let fileURL as URL in enumerator {
            scannedCount += 1
            
            do {
                // Efficiently get resource values in a single call
                let resourceValues = try fileURL.resourceValues(forKeys: Set(resourceKeys))
                
                // Skip non-regular files (directories, symlinks, etc.)
                guard resourceValues.isRegularFile == true else { continue }
                
                // Check file extension efficiently using file URL
                let pathExtension = fileURL.pathExtension.lowercased()
                
                if validDiskExtensions.contains(pathExtension) {
                    foundCount += 1
                    debugLog("Found disk image: \(fileURL.lastPathComponent)", category: .fileSystem)
                    
                    // Security: Validate file paths and types
                    if isValidDiskImageFile(fileURL) {
                        loadDiskImage(fileURL)
                    }
                }
            } catch {
                // Handle individual file access errors without stopping the scan
                warningLog("Failed to get resource values for file: \(fileURL.path) - \(error.localizedDescription)", category: .fileSystem)
                continue
            }
        }
        
        infoLog("Disk scan completed: \(foundCount) disk images found out of \(scannedCount) files scanned", category: .fileSystem)
    }
    
    
    
    // Security: Validate disk image files
    private func isValidDiskImageFile(_ url: URL) -> Bool {
        debugLog("Validating file: \(url.path)", category: .fileSystem)
        
        // Check if file exists and is within allowed directory
        guard FileManager.default.fileExists(atPath: url.path) else { 
            debugLog("File does not exist", category: .fileSystem)
            return false 
        }
        
        // Validate file extension
        let validExtensions = ["dim", "xdf", "d88", "hdm", "hdf"]
        let ext = url.pathExtension.lowercased()
        guard validExtensions.contains(ext) else { 
            debugLog("Invalid file extension: \(ext)", category: .fileSystem)
            return false 
        }
        
        // Prevent path traversal - ensure file is within allowed directories
        let standardizedURL = url.standardized
        let urlPath = standardizedURL.path
        debugLog("Standardized path: \(urlPath)", category: .fileSystem)
        
        // Priority check for Mobile Documents paths (common on actual devices)
        if urlPath.contains("/Mobile Documents/") {
            debugLog("Found Mobile Documents path", category: .fileSystem)
            if urlPath.contains("/Downloads/") || urlPath.contains("/Documents/") || urlPath.contains("/Desktop/") {
                infoLog("Allowing Mobile Documents access: \(urlPath)", category: .fileSystem)
                return true
            }
        }
        
        // Check for iCloud Documents path patterns (both simulator and device paths)
        if urlPath.contains("com~apple~CloudDocs") {
            debugLog("Found com~apple~CloudDocs path", category: .fileSystem)
            // Allow iCloud Drive access for Downloads, Documents, or other common folders
            let iCloudAllowedFolders = ["Downloads", "Documents", "Desktop"]
            for folder in iCloudAllowedFolders {
                if urlPath.contains("/\(folder)/") || urlPath.hasSuffix("/\(folder)") {
                    infoLog("Allowing iCloud \(folder) folder access: \(urlPath)", category: .fileSystem)
                    return true
                }
            }
        }
        
        // Check for File Provider Storage paths (iOS document provider extension)
        if urlPath.contains("/File Provider Storage/") {
            debugLog("Found File Provider Storage path", category: .fileSystem)
            infoLog("Allowing File Provider Storage access: \(urlPath)", category: .fileSystem)
            return true
        }
        
        // Get app's Documents directory
        guard let documentsURL = try? FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: false) else { 
            errorLog("Could not get documents directory", category: .fileSystem)
            return false 
        }
        
        var allowedPaths = [
            documentsURL.appendingPathComponent("X68000"),
            documentsURL.appendingPathComponent("Documents"), // Legacy path for backward compatibility
            documentsURL.appendingPathComponent("Inbox")
        ]
        
        // Also allow Downloads folder (standard user Downloads directory)
        if let downloadsURL = try? FileManager.default.url(for: .downloadsDirectory, in: .userDomainMask, appropriateFor: nil, create: false) {
            allowedPaths.append(downloadsURL)
            debugLog("Added Downloads directory: \(downloadsURL.path)", category: .fileSystem)
        }
        
        // Also allow iCloud Drive Downloads folder (common for shared files)
        if let iCloudURL = FileManager.default.url(forUbiquityContainerIdentifier: nil) {
            allowedPaths.append(iCloudURL.appendingPathComponent("Downloads"))
        }
        
        // Check if the file URL is within any of the allowed directories
        debugLog("Checking allowed paths...", category: .fileSystem)
        for allowedPath in allowedPaths {
            let standardizedAllowedURL = allowedPath.standardized
            debugLog("Checking against: \(standardizedAllowedURL.path)", category: .fileSystem)
            if standardizedURL.path.hasPrefix(standardizedAllowedURL.path) {
                debugLog("Path validation passed via allowedPaths", category: .fileSystem)
                return true
            }
        }
        
        debugLog("Path validation failed - no matching allowed paths", category: .fileSystem)
        debugLog("Full analysis - path contains Mobile Documents: \(urlPath.contains("/Mobile Documents/"))", category: .fileSystem)
        debugLog("Full analysis - path contains Downloads: \(urlPath.contains("/Downloads/"))", category: .fileSystem)
        debugLog("Full analysis - path contains Documents: \(urlPath.contains("/Documents/"))", category: .fileSystem)
        return false
    }
    
    func loadAsynchronously(_ url: URL?) -> Void {
        guard let url = url else { 
            errorLog("loadAsynchronously called with nil URL", category: .fileSystem)
            return 
        }
        
        debugLog("loadAsynchronously called with: \(url.lastPathComponent)", category: .fileSystem)
        debugLog("Loading file: \(url.path)", category: .fileSystem)
        
        // Use a simple approach without complex queue nesting
        self.handleiCloudFileLoading(url: url)
    }
    
    private func handleiCloudFileLoading(url: URL) {
        debugLog("handleiCloudFileLoading called with: \(url.lastPathComponent)", category: .fileSystem)
        // Start security-scoped access for iCloud files immediately
        guard url.startAccessingSecurityScopedResource() else {
            errorLog("Failed to start accessing security-scoped resource for iCloud file", category: .fileSystem)
            // Clean up loading state on failure
            if let gameScene = self.gameScene {
                DispatchQueue.main.async {
                    gameScene.clearLoadingFile(url)
                }
            }
            return
        }
        debugLog("Security-scoped resource access started successfully", category: .fileSystem)
        
        defer {
            url.stopAccessingSecurityScopedResource()
            // Clean up loading state after security scope is released
            if let gameScene = self.gameScene {
                DispatchQueue.main.async {
                    gameScene.clearLoadingFile(url)
                }
            }
        }
        
        // Check if this is an iCloud file that needs downloading
        if url.path.contains("/Mobile Documents/") || url.path.contains("com~apple~CloudDocs") {
            infoLog("Detected iCloud file, attempting to download...", category: .fileSystem)
            
            do {
                // First check if file is already available
                if FileManager.default.fileExists(atPath: url.path) {
                    infoLog("iCloud file already exists locally", category: .fileSystem)
                } else {
                    // Try to download the file from iCloud with security scope
                    try FileManager.default.startDownloadingUbiquitousItem(at: url)
                    infoLog("Download request sent to iCloud", category: .fileSystem)
                    
                    // Wait for download with timeout but don't keep requesting
                    let timeout = 30.0 // 30 seconds timeout
                    let startTime = Date()
                    var downloadRequested = true
                    
                    while true {
                        if Date().timeIntervalSince(startTime) > timeout {
                            warningLog("Timeout waiting for iCloud download", category: .fileSystem)
                            return
                        }
                        
                        // Check download status
                        do {
                            let resourceValues = try url.resourceValues(forKeys: [.ubiquitousItemDownloadingStatusKey])
                            
                            if let status = resourceValues.ubiquitousItemDownloadingStatus {
                                debugLog("Download status: \(status.rawValue)", category: .fileSystem)
                                if status == .current {
                                    infoLog("iCloud file download completed", category: .fileSystem)
                                    break
                                } else if status == .notDownloaded && !downloadRequested {
                                    infoLog("File not downloaded, requesting download...", category: .fileSystem)
                                    try FileManager.default.startDownloadingUbiquitousItem(at: url)
                                    downloadRequested = true
                                } else if status == .notDownloaded && downloadRequested {
                                    debugLog("Download request sent, waiting...", category: .fileSystem)
                                }
                            } else {
                                // If we can't get status, try to access the file directly
                                if FileManager.default.fileExists(atPath: url.path) {
                                    infoLog("iCloud file exists locally", category: .fileSystem)
                                    break
                                }
                            }
                        } catch {
                            errorLog("Error checking download status", error: error, category: .fileSystem)
                            // Fallback: check if file exists locally
                            if FileManager.default.fileExists(atPath: url.path) {
                                infoLog("File exists locally (fallback check)", category: .fileSystem)
                                break
                            }
                        }
                        
                        Thread.sleep(forTimeInterval: 1.0) // Increased to 1 second
                    }
                }
            } catch let error as NSError {
                errorLog("Error downloading iCloud file", error: error, category: .fileSystem)
                // Try to proceed anyway if the file exists
                if !FileManager.default.fileExists(atPath: url.path) {
                    return
                }
            }
        }
        
        // Now validate the file after ensuring it's downloaded
        // For iCloud files, use a simpler validation that doesn't require file existence check
        let validExtensions = ["dim", "xdf", "d88", "hdm", "hdf"]
        let ext = url.pathExtension.lowercased()
        guard validExtensions.contains(ext) else {
            errorLog("Invalid file extension: \(ext)", category: .fileSystem)
            return
        }
        
        // For iCloud files, trust the system's security model
        if url.path.contains("/Mobile Documents/") || url.path.contains("com~apple~CloudDocs") {
            infoLog("Security: Allowing iCloud file access: \(url.path)", category: .fileSystem)
        } else {
            // Use normal validation for non-iCloud files
            guard self.isValidDiskImageFile(url) else {
                errorLog("Security: Invalid or unsafe file path: \(url.path)", category: .fileSystem)
                return
            }
        }
        
        infoLog("Security: File validation passed for: \(url.lastPathComponent)", category: .fileSystem)
        
        // Continue with actual file loading - security scope already active
        // Call performFileLoad synchronously to maintain security scope
        self.performFileLoad(url: url)
    }
    
    private func performFileLoad(url: URL) {
        do {
            // File should be accessible now with security scope
            let imageData: Data = try Data(contentsOf: url)
            
            // Security: Validate file size (max 80MB for disk images)
            let maxSize = 80 * 1024 * 1024 // 80MB
            guard imageData.count <= maxSize else {
                errorLog("Security: File too large: \(imageData.count) bytes", category: .fileSystem)
                return
            }
            
            debugLog("size: \(imageData.count)", category: .fileSystem)
            
            // UI updates need to be on main thread
            DispatchQueue.main.async {
                let extname = url.pathExtension.removingPercentEncoding
                if extname?.lowercased() == "hdf" {
                    if let p = X68000_GetDiskImageBufferPointer(4, imageData.count) {
                        imageData.copyBytes(to: p, count: imageData.count)
                        X68000_LoadHDD(url.absoluteString)
                        infoLog("HDD loaded successfully - no automatic reset", category: .fileSystem)
                    }
                } else {
                    var drive = 0
                    if url.path.contains(" B.") || url.path.contains("_B.") {
                        drive = 1
                    }
                    
                    if let p = X68000_GetDiskImageBufferPointer(drive, imageData.count) {
                        imageData.copyBytes(to: p, count: imageData.count)
                        X68000_LoadFDD(drive, url.path)
                        infoLog("FDD loaded successfully (drive \(drive)) - no automatic reset", category: .fileSystem)
                    }
                }
            }
        } catch let error as NSError {
            errorLog("Error loading file data", error: error, category: .fileSystem)
        }
    }
    func loadDiskImage( _ url : URL )
    {
        debugLog("FileSystem.loadDiskImage called with: \(url.lastPathComponent)", category: .fileSystem)
        debugLog("Full path: \(url.path)", category: .fileSystem)
        debugLog("File extension: \(url.pathExtension)", category: .fileSystem)
        saveSRAM()
        //        X68000_Reset()
        
        // Check for A/B disk pair functionality
        if shouldCheckForDiskPair(url) {
            infoLog("A/B disk pair detected for: \(url.lastPathComponent)", category: .fileSystem)
            // For File Provider Storage, we can only reliably access the file the user clicked
            // So we'll try to load as a pair, but fall back to single disk if companion fails
            if checkForCompanionDisk(url) {
                infoLog("Companion disk might exist, attempting A/B pair load with fallback", category: .fileSystem)
                loadDiskPairWithFallback(primaryUrl: url)
            } else {
                infoLog("No companion disk found, loading single disk", category: .fileSystem)
                loadAsynchronously( url )
            }
        } else {
            infoLog("Single disk load for: \(url.lastPathComponent)", category: .fileSystem)
            loadAsynchronously( url )
        }
    }
    
    // Check if this file should trigger A/B disk pair loading
    private func shouldCheckForDiskPair(_ url: URL) -> Bool {
        let filename = url.deletingPathExtension().lastPathComponent.lowercased()
        // Check for various A/B patterns: "a", " a", "_a", "a.", " b", "_b", "b."
        return filename.hasSuffix("a") || filename.hasSuffix("b") || 
               filename.hasSuffix(" a") || filename.hasSuffix(" b") ||
               filename.hasSuffix("_a") || filename.hasSuffix("_b")
    }
    
    // Check if the companion disk (A or B) actually exists
    private func checkForCompanionDisk(_ url: URL) -> Bool {
        let filename = url.deletingPathExtension().lastPathComponent
        let fileExtension = url.pathExtension
        let directory = url.deletingLastPathComponent()
        let lowercaseFilename = filename.lowercased()
        
        // Extract base filename and determine if this is A or B
        var baseFilename = filename
        var isCurrentA = false
        var isCurrentB = false
        
        if lowercaseFilename.hasSuffix(" a") {
            baseFilename = String(filename.dropLast(2))
            isCurrentA = true
        } else if lowercaseFilename.hasSuffix(" b") {
            baseFilename = String(filename.dropLast(2))
            isCurrentB = true
        } else if lowercaseFilename.hasSuffix("_a") {
            baseFilename = String(filename.dropLast(2))
            isCurrentA = true
        } else if lowercaseFilename.hasSuffix("_b") {
            baseFilename = String(filename.dropLast(2))
            isCurrentB = true
        } else if lowercaseFilename.hasSuffix("a") {
            baseFilename = String(filename.dropLast())
            isCurrentA = true
        } else if lowercaseFilename.hasSuffix("b") {
            baseFilename = String(filename.dropLast())
            isCurrentB = true
        }
        
        // Generate companion filename
        let companionFilename: String
        if isCurrentA {
            // Current is A, look for B
            if lowercaseFilename.hasSuffix(" a") {
                companionFilename = baseFilename + " B"
            } else if lowercaseFilename.hasSuffix("_a") {
                companionFilename = baseFilename + "_B"
            } else {
                companionFilename = baseFilename + "B"
            }
        } else if isCurrentB {
            // Current is B, look for A
            if lowercaseFilename.hasSuffix(" b") {
                companionFilename = baseFilename + " A"
            } else if lowercaseFilename.hasSuffix("_b") {
                companionFilename = baseFilename + "_A"
            } else {
                companionFilename = baseFilename + "A"
            }
        } else {
            return false
        }
        
        let companionUrl = directory.appendingPathComponent(companionFilename).appendingPathExtension(fileExtension)
        
        // For File Provider Storage and iCloud files, we can't easily check existence without security scope
        // So we'll assume the companion exists and let the loading process handle missing files
        if url.path.contains("/File Provider Storage/") || 
           url.path.contains("/Mobile Documents/") || 
           url.path.contains("com~apple~CloudDocs") {
            debugLog("Assuming companion exists for cloud storage: \(companionUrl.lastPathComponent)", category: .fileSystem)
            return true
        }
        
        // For local files, check if companion actually exists
        let companionExists = FileManager.default.fileExists(atPath: companionUrl.path)
        debugLog("Companion disk check - looking for: \(companionUrl.lastPathComponent), exists: \(companionExists)", category: .fileSystem)
        return companionExists
    }
    
    // Load A/B disk pair with fallback to single disk
    private func loadDiskPairWithFallback(primaryUrl: URL) {
        let filename = primaryUrl.deletingPathExtension().lastPathComponent
        let fileExtension = primaryUrl.pathExtension
        let directory = primaryUrl.deletingLastPathComponent()
        let lowercaseFilename = filename.lowercased()
        
        // Extract base filename
        var baseFilename = filename
        if lowercaseFilename.hasSuffix(" a") {
            baseFilename = String(filename.dropLast(2))
        } else if lowercaseFilename.hasSuffix(" b") {
            baseFilename = String(filename.dropLast(2))
        } else if lowercaseFilename.hasSuffix("_a") {
            baseFilename = String(filename.dropLast(2))
        } else if lowercaseFilename.hasSuffix("_b") {
            baseFilename = String(filename.dropLast(2))
        } else if lowercaseFilename.hasSuffix("a") {
            baseFilename = String(filename.dropLast())
        } else if lowercaseFilename.hasSuffix("b") {
            baseFilename = String(filename.dropLast())
        }
        
        // Generate pair identifier for duplicate prevention
        let pairIdentifier = directory.path + "/" + baseFilename.lowercased()
        
        // Thread-safe check and set for loading pair
        FileSystem.loadingPairLock.lock()
        defer { FileSystem.loadingPairLock.unlock() }
        
        // Check if we're already loading this pair
        if let currentPair = FileSystem.currentlyLoadingPair, currentPair == pairIdentifier {
            warningLog("Already loading disk pair: \(pairIdentifier), skipping", category: .fileSystem)
            if let gameScene = self.gameScene {
                gameScene.clearLoadingFile(primaryUrl)
            }
            return
        }
        
        // Mark this pair as currently loading
        FileSystem.currentlyLoadingPair = pairIdentifier
        debugLog("Set currently loading pair to: \(pairIdentifier)", category: .fileSystem)
        
        // Generate A and B filenames
        let aFilename: String
        let bFilename: String
        
        if lowercaseFilename.hasSuffix(" a") || lowercaseFilename.hasSuffix(" b") {
            aFilename = baseFilename + " A"
            bFilename = baseFilename + " B"
        } else if lowercaseFilename.hasSuffix("_a") || lowercaseFilename.hasSuffix("_b") {
            aFilename = baseFilename + "_A"
            bFilename = baseFilename + "_B"
        } else {
            aFilename = baseFilename + "A"
            bFilename = baseFilename + "B"
        }
        
        let aUrl = directory.appendingPathComponent(aFilename).appendingPathExtension(fileExtension)
        let bUrl = directory.appendingPathComponent(bFilename).appendingPathExtension(fileExtension)
        
        infoLog("A/B disk pair attempt:", category: .fileSystem)
        infoLog("A disk: \(aUrl.path)", category: .fileSystem)
        infoLog("B disk: \(bUrl.path)", category: .fileSystem)
        infoLog("Primary (clicked): \(primaryUrl.path)", category: .fileSystem)
        
        // Try to load the primary disk first (the one user clicked)
        loadAsynchronously(primaryUrl)
        
        // Try to load the companion disk, but don't fail if it doesn't work
        let companionUrl = (primaryUrl.path == aUrl.path) ? bUrl : aUrl
        debugLog("Attempting to load companion disk: \(companionUrl.lastPathComponent)", category: .fileSystem)
        
        // Load companion with fallback - don't block if it fails
        DispatchQueue.global(qos: .background).asyncAfter(deadline: .now() + 0.5) { [weak self] in
            self?.loadCompanionDiskWithFallback(companionUrl: companionUrl) {
                // Cleanup regardless of success/failure (thread-safe)
                FileSystem.loadingPairLock.lock()
                FileSystem.currentlyLoadingPair = nil
                FileSystem.loadingPairLock.unlock()
                debugLog("Cleared pair loading state for: \(pairIdentifier)", category: .fileSystem)
            }
        }
    }
    
    // Try to load companion disk, but don't fail if access is denied
    private func loadCompanionDiskWithFallback(companionUrl: URL, completion: @escaping () -> Void) {
        debugLog("Attempting companion disk load: \(companionUrl.lastPathComponent)", category: .fileSystem)
        
        // Try to start security scope for companion
        guard companionUrl.startAccessingSecurityScopedResource() else {
            infoLog("Cannot access companion disk \(companionUrl.lastPathComponent) - user did not grant permission", category: .fileSystem)
            completion()
            return
        }
        
        defer {
            companionUrl.stopAccessingSecurityScopedResource()
        }
        
        // Try to load companion disk
        do {
            let imageData = try Data(contentsOf: companionUrl)
            debugLog("Successfully read companion disk: \(companionUrl.lastPathComponent)", category: .fileSystem)
            
            // Determine drive based on filename
            var drive = 0
            let filename = companionUrl.deletingPathExtension().lastPathComponent.lowercased()
            if filename.hasSuffix("b") || filename.hasSuffix(" b") || filename.hasSuffix("_b") {
                drive = 1
            }
            
            DispatchQueue.main.async {
                if let p = X68000_GetDiskImageBufferPointer(drive, imageData.count) {
                    imageData.copyBytes(to: p, count: imageData.count)
                    X68000_LoadFDD(drive, companionUrl.path)
                    infoLog("Success: Companion disk loaded: \(companionUrl.lastPathComponent) (drive \(drive))", category: .fileSystem)
                } else {
                    errorLog("Failed to get buffer for companion disk", category: .fileSystem)
                }
                completion()
            }
        } catch {
            infoLog("Could not load companion disk \(companionUrl.lastPathComponent): \(error)", category: .fileSystem)
            completion()
        }
    }
    
    // Load A/B disk pair (B first, then A)
    private func loadDiskPair(primaryUrl: URL) {
        let filename = primaryUrl.deletingPathExtension().lastPathComponent
        let fileExtension = primaryUrl.pathExtension
        let directory = primaryUrl.deletingLastPathComponent()
        
        // Generate both A and B filenames first to create a proper pair identifier
        var baseFilename = filename
        let lowercaseFilename = filename.lowercased()
        
        // Detect various A/B patterns and extract base filename
        if lowercaseFilename.hasSuffix(" a") {
            baseFilename = String(filename.dropLast(2)) // Remove ' A'
        } else if lowercaseFilename.hasSuffix(" b") {
            baseFilename = String(filename.dropLast(2)) // Remove ' B'
        } else if lowercaseFilename.hasSuffix("_a") {
            baseFilename = String(filename.dropLast(2)) // Remove '_A'
        } else if lowercaseFilename.hasSuffix("_b") {
            baseFilename = String(filename.dropLast(2)) // Remove '_B'
        } else if lowercaseFilename.hasSuffix("a") {
            baseFilename = String(filename.dropLast()) // Remove 'A'
        } else if lowercaseFilename.hasSuffix("b") {
            baseFilename = String(filename.dropLast()) // Remove 'B'
        }
        
        // Generate a unique identifier for this disk pair to prevent duplicates
        let pairIdentifier = directory.path + "/" + baseFilename.lowercased()
        
        debugLog("Checking pair identifier: \(pairIdentifier)", category: .fileSystem)
        
        // Thread-safe check and set for loading pair
        FileSystem.loadingPairLock.lock()
        let currentlyLoadingStatus = FileSystem.currentlyLoadingPair
        FileSystem.loadingPairLock.unlock()
        
        debugLog("Currently loading: \(currentlyLoadingStatus ?? "none")", category: .fileSystem)
        
        FileSystem.loadingPairLock.lock()
        defer { FileSystem.loadingPairLock.unlock() }
        
        // Check if we're already loading this pair - only check at the pair level
        if let currentPair = FileSystem.currentlyLoadingPair, currentPair == pairIdentifier {
            warningLog("Already loading disk pair: \(pairIdentifier), skipping", category: .fileSystem)
            // Clean up the individual file tracking for this file since we're skipping the pair
            if let gameScene = self.gameScene {
                gameScene.clearLoadingFile(primaryUrl)
            }
            return
        }
        
        // Mark this pair as currently loading
        FileSystem.currentlyLoadingPair = pairIdentifier
        debugLog("Set currently loading pair to: \(pairIdentifier)", category: .fileSystem)
        
        // Create completion handler that clears the pair loading state
        let completePairLoading = {
            FileSystem.loadingPairLock.lock()
            FileSystem.currentlyLoadingPair = nil
            FileSystem.loadingPairLock.unlock()
            print("Debug: Cleared pair loading state for: \(pairIdentifier)")
        }
        
        // Generate A and B filenames with the same separator pattern
        let aFilename: String
        let bFilename: String
        
        if lowercaseFilename.hasSuffix(" a") || lowercaseFilename.hasSuffix(" b") {
            aFilename = baseFilename + " A"
            bFilename = baseFilename + " B"
        } else if lowercaseFilename.hasSuffix("_a") || lowercaseFilename.hasSuffix("_b") {
            aFilename = baseFilename + "_A"
            bFilename = baseFilename + "_B"
        } else {
            aFilename = baseFilename + "A"
            bFilename = baseFilename + "B"
        }
        let aUrl = directory.appendingPathComponent(aFilename).appendingPathExtension(fileExtension)
        let bUrl = directory.appendingPathComponent(bFilename).appendingPathExtension(fileExtension)
        
        infoLog("A/B disk pair detected:", category: .fileSystem)
        infoLog("A disk: \(aUrl.path)", category: .fileSystem)
        infoLog("B disk: \(bUrl.path)", category: .fileSystem)
        
        // Check which files exist
        var existingUrls: [URL] = []
        
        // Start security-scoped access for files that need it
        let needsSecurityScope = aUrl.path.contains("/Mobile Documents/") || 
                                aUrl.path.contains("com~apple~CloudDocs") ||
                                aUrl.path.contains("/File Provider Storage/")
        
        if needsSecurityScope {
            // For iCloud files and File Provider Storage, use the file existence check after security scope
            loadDiskPairWithSecurityScope(aUrl: aUrl, bUrl: bUrl, originalUrl: primaryUrl, completion: completePairLoading)
        } else {
            // For local files, check existence directly
            if FileManager.default.fileExists(atPath: bUrl.path) {
                existingUrls.append(bUrl)
            }
            if FileManager.default.fileExists(atPath: aUrl.path) {
                existingUrls.append(aUrl)
            }
            
            if existingUrls.isEmpty {
                warningLog("Neither A nor B disk found, loading single disk", category: .fileSystem)
                loadAsynchronously(primaryUrl)
                completePairLoading()
            } else {
                loadMultipleDiskImages(existingUrls, completion: completePairLoading)
            }
        }
    }
    
    // Handle A/B disk pair loading with security scope (iCloud and File Provider Storage)
    private func loadDiskPairWithSecurityScope(aUrl: URL, bUrl: URL, originalUrl: URL, completion: @escaping () -> Void) {
        debugLog("loadDiskPairWithSecurityScope called", category: .fileSystem)
        debugLog("A URL: \(aUrl.path)", category: .fileSystem)
        debugLog("B URL: \(bUrl.path)", category: .fileSystem)
        debugLog("Original URL (with security scope): \(originalUrl.path)", category: .fileSystem)
        
        // Load both files with the same security scope session
        // We'll use a single security scope session to load both files
        loadDiskPairWithSharedSecurityScope(aUrl: aUrl, bUrl: bUrl, originalUrl: originalUrl, completion: completion)
    }
    
    // Load A/B disk pair with shared security scope
    private func loadDiskPairWithSharedSecurityScope(aUrl: URL, bUrl: URL, originalUrl: URL, completion: @escaping () -> Void) {
        debugLog("loadDiskPairWithSharedSecurityScope called", category: .fileSystem)
        debugLog("Using original URL for security scope: \(originalUrl.lastPathComponent)", category: .fileSystem)
        
        DispatchQueue.global(qos: .background).async { [weak self] in
            guard let self = self else {
                DispatchQueue.main.async { completion() }
                return
            }
            
            // Start security scope with the original URL (the one user clicked)
            guard originalUrl.startAccessingSecurityScopedResource() else {
                errorLog("Failed to start security scope with original URL: \(originalUrl.lastPathComponent)", category: .fileSystem)
                DispatchQueue.main.async { completion() }
                return
            }
            
            defer {
                originalUrl.stopAccessingSecurityScopedResource()
                debugLog("Released security scope for: \(originalUrl.lastPathComponent)", category: .fileSystem)
            }
            
            // Now load both files within the same security scope
            let group = DispatchGroup()
            
            // Load B disk
            group.enter()
            self.loadDiskFileWithData(bUrl) { success in
                if success {
                    infoLog("B disk loaded successfully", category: .fileSystem)
                } else {
                    warningLog("B disk failed to load", category: .fileSystem)
                }
                group.leave()
            }
            
            // Load A disk
            group.enter()
            self.loadDiskFileWithData(aUrl) { success in
                if success {
                    infoLog("A disk loaded successfully", category: .fileSystem)
                } else {
                    warningLog("A disk failed to load", category: .fileSystem)
                }
                group.leave()
            }
            
            group.notify(queue: .main) {
                infoLog("A/B disk pair loading completed - no automatic reset", category: .fileSystem)
                completion()
            }
        }
    }
    
    // Load disk file with data (assumes security scope is already active)
    private func loadDiskFileWithData(_ url: URL, completion: @escaping (Bool) -> Void) {
        debugLog("loadDiskFileWithData called for: \(url.lastPathComponent)", category: .fileSystem)
        
        // Validate file extension
        let validExtensions = ["dim", "xdf", "d88", "hdm", "hdf"]
        let ext = url.pathExtension.lowercased()
        guard validExtensions.contains(ext) else {
            errorLog("Invalid file extension: \(ext)", category: .fileSystem)
            completion(false)
            return
        }
        
        do {
            debugLog("Reading data from: \(url.path)", category: .fileSystem)
            let imageData = try Data(contentsOf: url)
            debugLog("Successfully read \(imageData.count) bytes from \(url.lastPathComponent)", category: .fileSystem)
            
            // Determine drive based on filename
            var drive = 0
            let filename = url.deletingPathExtension().lastPathComponent.lowercased()
            if filename.hasSuffix("b") || filename.hasSuffix(" b") || filename.hasSuffix("_b") || 
               url.path.contains(" B.") || url.path.contains("_B.") {
                drive = 1
            }
            debugLog("Determined drive: \(drive) for \(url.lastPathComponent)", category: .fileSystem)
            
            DispatchQueue.main.async {
                let extname = url.pathExtension.lowercased()
                if extname == "hdf" {
                    debugLog("Loading as HDD image", category: .fileSystem)
                    if let p = X68000_GetDiskImageBufferPointer(4, imageData.count) {
                        imageData.copyBytes(to: p, count: imageData.count)
                        X68000_LoadHDD(url.absoluteString)
                        infoLog("Success: HDD loaded: \(url.lastPathComponent)", category: .fileSystem)
                        completion(true)
                    } else {
                        errorLog("Failed to get HDD buffer pointer", category: .fileSystem)
                        completion(false)
                    }
                } else {
                    debugLog("Loading as FDD image to drive \(drive)", category: .fileSystem)
                    if let p = X68000_GetDiskImageBufferPointer(drive, imageData.count) {
                        imageData.copyBytes(to: p, count: imageData.count)
                        X68000_LoadFDD(drive, url.path)
                        infoLog("Success: FDD loaded: \(url.lastPathComponent) (drive \(drive))", category: .fileSystem)
                        completion(true)
                    } else {
                        errorLog("Failed to get FDD buffer pointer for drive \(drive)", category: .fileSystem)
                        completion(false)
                    }
                }
            }
        } catch {
            errorLog("Failed to read data from \(url.lastPathComponent)", error: error, category: .fileSystem)
            completion(false)
        }
    }
    
    // Load multiple disk images in sequence (B first, then A)
    private func loadMultipleDiskImages(_ urls: [URL], completion: @escaping () -> Void) {
        guard !urls.isEmpty else { 
            completion()
            return 
        }
        
        // Sort to ensure B is loaded before A
        let sortedUrls = urls.sorted { url1, url2 in
            let name1 = url1.deletingPathExtension().lastPathComponent.lowercased()
            let name2 = url2.deletingPathExtension().lastPathComponent.lowercased()
            
            let isBDisk1 = name1.hasSuffix("b") || name1.hasSuffix(" b") || name1.hasSuffix("_b")
            let isADisk1 = name1.hasSuffix("a") || name1.hasSuffix(" a") || name1.hasSuffix("_a")
            let isBDisk2 = name2.hasSuffix("b") || name2.hasSuffix(" b") || name2.hasSuffix("_b")
            let isADisk2 = name2.hasSuffix("a") || name2.hasSuffix(" a") || name2.hasSuffix("_a")
            
            // B comes before A
            if isBDisk1 && isADisk2 {
                return true
            } else if isADisk1 && isBDisk2 {
                return false
            }
            return name1 < name2
        }
        
        loadDiskSequentially(urls: sortedUrls, index: 0, completion: completion)
    }
    
    // Load disks sequentially
    private func loadDiskSequentially(urls: [URL], index: Int, completion: @escaping () -> Void) {
        guard index < urls.count else { 
            completion()
            return 
        }
        
        let url = urls[index]
        let _ = (index == urls.count - 1)  // isLastDisk - potentially needed for future logic
        let filename = url.deletingPathExtension().lastPathComponent.lowercased()
        let _ = filename.hasSuffix("a") || filename.hasSuffix(" a") || filename.hasSuffix("_a")  // isADisk - potentially needed for future logic
        
        loadDiskImageWithCallback(url) { [weak self] success in
            if success {
                infoLog("Disk \(url.lastPathComponent) loaded successfully", category: .fileSystem)
                
                infoLog("Disk sequence loaded - no automatic reset", category: .fileSystem)
            }
            
            // Load next disk or complete
            if index + 1 < urls.count {
                self?.loadDiskSequentially(urls: urls, index: index + 1, completion: completion)
            } else {
                completion()
            }
        }
    }
    
    // Load disk image with completion callback
    private func loadDiskImageWithCallback(_ url: URL, completion: @escaping (Bool) -> Void) {
        debugLog("loadDiskImageWithCallback called for: \(url.lastPathComponent)", category: .fileSystem)
        DispatchQueue.global(qos: .background).async { [weak self] in
            debugLog("Background queue started for: \(url.lastPathComponent)", category: .fileSystem)
            guard let self = self else {
                debugLog("Self is nil, completing with false", category: .fileSystem)
                DispatchQueue.main.async { completion(false) }
                return
            }
            
            // Start security-scoped access for files that need it
            var needsSecurityScope = false
            if url.path.contains("/Mobile Documents/") || 
               url.path.contains("com~apple~CloudDocs") ||
               url.path.contains("/File Provider Storage/") {
                needsSecurityScope = true
                debugLog("Starting security-scoped access for: \(url.path)", category: .fileSystem)
                guard url.startAccessingSecurityScopedResource() else {
                    errorLog("Failed to start accessing security-scoped resource for: \(url.lastPathComponent)", category: .fileSystem)
                    errorLog("Full path: \(url.path)", category: .fileSystem)
                    DispatchQueue.main.async { completion(false) }
                    return
                }
                debugLog("Security-scoped access started successfully for: \(url.lastPathComponent)", category: .fileSystem)
            }
            
            defer {
                if needsSecurityScope {
                    url.stopAccessingSecurityScopedResource()
                }
                // Clean up loading state after security scope is released
                if let gameScene = self.gameScene {
                    DispatchQueue.main.async {
                        gameScene.clearLoadingFile(url)
                    }
                }
            }
            
            // Handle iCloud download if needed
            if needsSecurityScope {
                self.handleiCloudDownload(url: url) { success in
                    if success {
                        self.performActualFileLoad(url: url, completion: completion)
                    } else {
                        DispatchQueue.main.async { completion(false) }
                    }
                }
            } else {
                self.performActualFileLoad(url: url, completion: completion)
            }
        }
    }
    
    // Handle iCloud download
    private func handleiCloudDownload(url: URL, completion: @escaping (Bool) -> Void) {
        debugLog("handleiCloudDownload called for: \(url.lastPathComponent)", category: .fileSystem)
        do {
            let fileExists = FileManager.default.fileExists(atPath: url.path)
            debugLog("File exists check: \(fileExists) for \(url.path)", category: .fileSystem)
            
            if !fileExists {
                debugLog("File doesn't exist, attempting to download from iCloud", category: .fileSystem)
                try FileManager.default.startDownloadingUbiquitousItem(at: url)
                
                let timeout = 30.0
                let startTime = Date()
                
                while !FileManager.default.fileExists(atPath: url.path) {
                    if Date().timeIntervalSince(startTime) > timeout {
                        errorLog("Timeout downloading: \(url.lastPathComponent)", category: .fileSystem)
                        completion(false)
                        return
                    }
                    Thread.sleep(forTimeInterval: 0.5)
                }
                debugLog("Download completed for: \(url.lastPathComponent)", category: .fileSystem)
            } else {
                debugLog("File already exists locally: \(url.lastPathComponent)", category: .fileSystem)
            }
            completion(true)
        } catch {
            errorLog("Error downloading: \(url.lastPathComponent)", error: error, category: .fileSystem)
            completion(false)
        }
    }
    
    // Perform actual file loading
    private func performActualFileLoad(url: URL, completion: @escaping (Bool) -> Void) {
        debugLog("performActualFileLoad called for: \(url.lastPathComponent)", category: .fileSystem)
        
        // Validate file
        let validExtensions = ["dim", "xdf", "d88", "hdm", "hdf"]
        let ext = url.pathExtension.lowercased()
        guard validExtensions.contains(ext) else {
            errorLog("Invalid file extension: \(ext)", category: .fileSystem)
            DispatchQueue.main.async { completion(false) }
            return
        }
        debugLog("File extension validation passed: \(ext)", category: .fileSystem)
        
        do {
            debugLog("Attempting to read data from: \(url.path)", category: .fileSystem)
            let imageData = try Data(contentsOf: url)
            debugLog("Successfully read \(imageData.count) bytes from \(url.lastPathComponent)", category: .fileSystem)
            
            // Determine drive based on filename
            var drive = 0
            let filename = url.deletingPathExtension().lastPathComponent.lowercased()
            if filename.hasSuffix("b") || filename.hasSuffix(" b") || filename.hasSuffix("_b") || 
               url.path.contains(" B.") || url.path.contains("_B.") {
                drive = 1
            }
            debugLog("Determined drive: \(drive) for \(url.lastPathComponent)", category: .fileSystem)
            
            DispatchQueue.main.async {
                let extname = url.pathExtension.lowercased()
                if extname == "hdf" {
                    debugLog("Loading as HDD image", category: .fileSystem)
                    if let p = X68000_GetDiskImageBufferPointer(4, imageData.count) {
                        imageData.copyBytes(to: p, count: imageData.count)
                        X68000_LoadHDD(url.absoluteString)
                        infoLog("Success: HDD loaded: \(url.lastPathComponent)", category: .fileSystem)
                        completion(true)
                    } else {
                        errorLog("Failed to get HDD buffer pointer", category: .fileSystem)
                        completion(false)
                    }
                } else {
                    debugLog("Loading as FDD image to drive \(drive)", category: .fileSystem)
                    if let p = X68000_GetDiskImageBufferPointer(drive, imageData.count) {
                        imageData.copyBytes(to: p, count: imageData.count)
                        X68000_LoadFDD(drive, url.path)
                        infoLog("Success: FDD loaded: \(url.lastPathComponent) (drive \(drive))", category: .fileSystem)
                        completion(true)
                    } else {
                        errorLog("Failed to get FDD buffer pointer for drive \(drive)", category: .fileSystem)
                        completion(false)
                    }
                }
            }
        } catch {
            print("Error: Failed to load file data from \(url.lastPathComponent): \(error)")
            DispatchQueue.main.async { completion(false) }
        }
    }
    func saveSRAM()
    {
        infoLog("==== Save SRAM ====", category: .fileSystem)
        guard let sramPointer = X68000_GetSRAMPointer() else {
            errorLog("Failed to get SRAM pointer", category: .fileSystem)
            return
        }
        
        let data = Data(bytes: sramPointer, count: 0x4000)
        guard let url = getDocumentsPath("SRAM.DAT") else {
            errorLog("Failed to get SRAM file path", category: .fileSystem)
            return
        }
        
        do {
            try data.write(to: url)
            infoLog("SRAM.DAT saved successfully (\(data.count) bytes) to: \(url.path)", category: .fileSystem)
        } catch let error as NSError {
            errorLog("Failed to save SRAM.DAT", error: error, category: .fileSystem)
        }
    }
    
    func loadSRAM()
    {
        infoLog("==== Load SRAM ====", category: .fileSystem)
        guard let url = findFileInDocuments("SRAM.DAT") else {
            infoLog("SRAM.DAT not found - starting with blank SRAM", category: .fileSystem)
            return
        }
        
        do {
            let data: Data = try Data(contentsOf: url)
            // Security: Validate SRAM file size (should be exactly 0x4000 bytes)
            guard data.count == 0x4000 else {
                errorLog("Security: Invalid SRAM file size: \(data.count), expected 0x4000", category: .fileSystem)
                return
            }
            if let p = X68000_GetSRAMPointer() {
                data.copyBytes(to: p, count: data.count)
                infoLog("SRAM.DAT loaded successfully (\(data.count) bytes)", category: .fileSystem)
            } else {
                errorLog("Failed to get SRAM pointer", category: .fileSystem)
            }
        } catch let error as NSError {
            errorLog("Error loading SRAM.DAT", error: error, category: .fileSystem)
        }
    }
    // Added by Awed 2023/10/7
    func loadCGROM()
    {
        infoLog("==== Load CGROM ====", category: .fileSystem)
        guard let url = findFileInDocuments("CGROM.DAT") else {
            infoLog("CGROM.DAT not found - will use embedded ROM", category: .fileSystem)
            return
        }
        
        do {
            let data: Data = try Data(contentsOf: url)
            // Security: Validate CGROM file size (typical size is 0xc0000 bytes)
            guard data.count > 0 && data.count <= 0xc0000 else {
                errorLog("Security: CGROM file invalid size: \(data.count)", category: .fileSystem)
                return
            }
            if let p = X68000_GetCGROMPointer() {
                data.copyBytes(to: p, count: data.count)
                infoLog("CGROM.DAT loaded successfully (\(data.count) bytes)", category: .fileSystem)
            } else {
                errorLog("Failed to get CGROM pointer", category: .fileSystem)
            }
        } catch let error as NSError {
            errorLog("Error loading CGROM.DAT", error: error, category: .fileSystem)
        }
    }
    
    func loadIPLROM()
    {
        infoLog("==== Load IPLROM ====", category: .fileSystem)
        guard let url = findFileInDocuments("IPLROM.DAT") else {
            infoLog("IPLROM.DAT not found - will use embedded ROM", category: .fileSystem)
            return
        }
        
        do {
            let data: Data = try Data(contentsOf: url)
            // Security: Validate IPLROM file size (typical size is 0x40000 bytes)
            guard data.count > 0 && data.count <= 0x40000 else {
                errorLog("Security: IPLROM file invalid size: \(data.count)", category: .fileSystem)
                return
            }
            if let p = X68000_GetIPLROMPointer() {
                data.copyBytes(to: p, count: data.count)
                infoLog("IPLROM.DAT loaded successfully (\(data.count) bytes)", category: .fileSystem)
            } else {
                errorLog("Failed to get IPLROM pointer", category: .fileSystem)
            }
        } catch let error as NSError {
            errorLog("Error loading IPLROM.DAT", error: error, category: .fileSystem)
        }
    }
    
    // MARK: - Explicit FDD Drive Loading
    func loadFDDToDrive(_ url: URL, drive: Int) {
        debugLog("FileSystem.loadFDDToDrive() called with: \(url.lastPathComponent) to drive \(drive)", category: .fileSystem)
        
        do {
            let data = try Data(contentsOf: url)
            
            // Security: Validate file size for disk images
            guard data.count > 0 && data.count <= 2 * 1024 * 1024 else { // Max 2MB for floppy disk
                errorLog("Security: Disk image file invalid size: \(data.count)", category: .fileSystem)
                return
            }
            
            if let p = X68000_GetDiskImageBufferPointer(drive, data.count) {
                data.copyBytes(to: p, count: data.count)
                X68000_LoadFDD(drive, url.path)
                infoLog("Success: FDD loaded: \(url.lastPathComponent) to drive \(drive)", category: .fileSystem)
                
                infoLog("FDD loaded to drive \(drive) - no automatic reset", category: .fileSystem)
            } else {
                errorLog("Failed to get FDD buffer pointer for drive \(drive)", category: .fileSystem)
            }
        } catch let error as NSError {
            errorLog("Error loading FDD image", error: error, category: .fileSystem)
        }
    }
    
    // end
    
    
}
