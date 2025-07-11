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
                print(error)
            }
        }
    }
    
    func createDocumentsFolder() {
        // iCloudコンテナのURL
        //        let url = FileManager.default.url(forUbiquityContainerIdentifier: nil)
        let url = try? FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
        let path = (url?.appendingPathComponent("Documents"))!
        do {
            try FileManager.default.createDirectory(at: path, withIntermediateDirectories: true, attributes: nil)
        } catch let error as NSError {
            print(error)
        }
        
#if true
        let fileURL = getDocumentsPath("README.txt")
        let todayText = "POWER TO MAKE YOUR DREAM COME TRUE. Plase put CGROM.DAT and IPLROM.DAT here."
        if ( FileManager.default.fileExists( atPath: fileURL!.path ) == true ) {
        } else {
            do {
                try todayText.write(to: fileURL!, atomically: true, encoding: .utf8)
            }
            catch {
                print("write error")
            }
        }
#endif
    }
    
    func getDocumentsPath(_ filename: String )->URL? {
        // for iCloud
        //      let containerURL = FileManager.default.url(forUbiquityContainerIdentifier: nil)
        let containerURL = try? FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
        
        let documentsURL = containerURL?.appendingPathComponent("Documents")
        let url = documentsURL?.appendingPathComponent(filename)
        return url
    }
    
    // Search for file in multiple locations (Documents and Inbox)
    func findFileInDocuments(_ filename: String) -> URL? {
        guard let containerURL = try? FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: false) else { 
            return nil 
        }
        
        // Search locations in priority order
        let searchPaths = [
            containerURL.appendingPathComponent("Documents").appendingPathComponent(filename),
            containerURL.appendingPathComponent("Inbox").appendingPathComponent(filename),
            containerURL.appendingPathComponent(filename) // Direct in documents root
        ]
        
        for path in searchPaths {
            if FileManager.default.fileExists(atPath: path.path) {
                print("Found \(filename) at: \(path.path)")
                return path
            }
        }
        
        print("File \(filename) not found in any search location")
        return nil
    }
    
    func boot()
    {
        // for iCloud
        //      let containerURL = FileManager.default.url(forUbiquityContainerIdentifier: nil)
        let containerURL = try? FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
        
        // コンテナに追加するフォルダのパス
        if let documentsURL = containerURL?.appendingPathComponent("Documents") {
            let dir = getDir( documentsURL )
            
            for n in dir {
                if let filename = n {
                    // Security: Validate file paths and types
                    if isValidDiskImageFile(filename) {
                        loadDiskImage( filename )
                    }
                }
            }
            
        }
    }
    func getDir(_ path : URL ) -> [URL?]
    {
        guard let fileNames = try? FileManager.default.contentsOfDirectory(at: path, includingPropertiesForKeys: nil) else {
            return [nil]
        }
        //        for i in 0..<fileNames.count {
        //            print("\(i): \(fileNames[i])")
        //        }
        
        return fileNames
    }
    
    
    
    // Security: Validate disk image files
    private func isValidDiskImageFile(_ url: URL) -> Bool {
        print("Debug: Validating file: \(url.path)")
        
        // Check if file exists and is within allowed directory
        guard FileManager.default.fileExists(atPath: url.path) else { 
            print("Debug: File does not exist")
            return false 
        }
        
        // Validate file extension
        let validExtensions = ["dim", "xdf", "d88", "hdm", "hdf"]
        let ext = url.pathExtension.lowercased()
        guard validExtensions.contains(ext) else { 
            print("Debug: Invalid file extension: \(ext)")
            return false 
        }
        
        // Prevent path traversal - ensure file is within allowed directories
        let standardizedURL = url.standardized
        let urlPath = standardizedURL.path
        print("Debug: Standardized path: \(urlPath)")
        
        // Priority check for Mobile Documents paths (common on actual devices)
        if urlPath.contains("/Mobile Documents/") {
            print("Debug: Found Mobile Documents path")
            if urlPath.contains("/Downloads/") || urlPath.contains("/Documents/") || urlPath.contains("/Desktop/") {
                print("Allowing Mobile Documents access: \(urlPath)")
                return true
            }
        }
        
        // Check for iCloud Documents path patterns (both simulator and device paths)
        if urlPath.contains("com~apple~CloudDocs") {
            print("Debug: Found com~apple~CloudDocs path")
            // Allow iCloud Drive access for Downloads, Documents, or other common folders
            let iCloudAllowedFolders = ["Downloads", "Documents", "Desktop"]
            for folder in iCloudAllowedFolders {
                if urlPath.contains("/\(folder)/") || urlPath.hasSuffix("/\(folder)") {
                    print("Allowing iCloud \(folder) folder access: \(urlPath)")
                    return true
                }
            }
        }
        
        // Check for File Provider Storage paths (iOS document provider extension)
        if urlPath.contains("/File Provider Storage/") {
            print("Debug: Found File Provider Storage path")
            print("Allowing File Provider Storage access: \(urlPath)")
            return true
        }
        
        // Get app's Documents directory
        guard let documentsURL = try? FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: false) else { 
            print("Debug: Could not get documents directory")
            return false 
        }
        
        var allowedPaths = [
            documentsURL.appendingPathComponent("Documents"),
            documentsURL.appendingPathComponent("Inbox")
        ]
        
        // Also allow Downloads folder (standard user Downloads directory)
        if let downloadsURL = try? FileManager.default.url(for: .downloadsDirectory, in: .userDomainMask, appropriateFor: nil, create: false) {
            allowedPaths.append(downloadsURL)
            print("Debug: Added Downloads directory: \(downloadsURL.path)")
        }
        
        // Also allow iCloud Drive Downloads folder (common for shared files)
        if let iCloudURL = FileManager.default.url(forUbiquityContainerIdentifier: nil) {
            allowedPaths.append(iCloudURL.appendingPathComponent("Downloads"))
        }
        
        // Check if the file URL is within any of the allowed directories
        print("Debug: Checking allowed paths...")
        for allowedPath in allowedPaths {
            let standardizedAllowedURL = allowedPath.standardized
            print("Debug: Checking against: \(standardizedAllowedURL.path)")
            if standardizedURL.path.hasPrefix(standardizedAllowedURL.path) {
                print("Debug: Path validation passed via allowedPaths")
                return true
            }
        }
        
        print("Debug: Path validation failed - no matching allowed paths")
        print("Debug: Full analysis - path contains Mobile Documents: \(urlPath.contains("/Mobile Documents/"))")
        print("Debug: Full analysis - path contains Downloads: \(urlPath.contains("/Downloads/"))")
        print("Debug: Full analysis - path contains Documents: \(urlPath.contains("/Documents/"))")
        return false
    }
    
    func loadAsynchronously(_ url: URL?) -> Void {
        guard let url = url else { 
            print("🐛 loadAsynchronously called with nil URL")
            return 
        }
        
        print("🐛 loadAsynchronously called with: \(url.lastPathComponent)")
        print("🐛 Loading file: \(url.path)")
        
        // Use a simple approach without complex queue nesting
        self.handleiCloudFileLoading(url: url)
    }
    
    private func handleiCloudFileLoading(url: URL) {
        print("🐛 handleiCloudFileLoading called with: \(url.lastPathComponent)")
        // Start security-scoped access for iCloud files immediately
        guard url.startAccessingSecurityScopedResource() else {
            print("🐛 Failed to start accessing security-scoped resource for iCloud file")
            // Clean up loading state on failure
            if let gameScene = self.gameScene {
                DispatchQueue.main.async {
                    gameScene.clearLoadingFile(url)
                }
            }
            return
        }
        print("🐛 Security-scoped resource access started successfully")
        
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
            print("Detected iCloud file, attempting to download...")
            
            do {
                // First check if file is already available
                if FileManager.default.fileExists(atPath: url.path) {
                    print("iCloud file already exists locally")
                } else {
                    // Try to download the file from iCloud with security scope
                    try FileManager.default.startDownloadingUbiquitousItem(at: url)
                    print("Download request sent to iCloud")
                    
                    // Wait for download with timeout but don't keep requesting
                    let timeout = 30.0 // 30 seconds timeout
                    let startTime = Date()
                    var downloadRequested = true
                    
                    while true {
                        if Date().timeIntervalSince(startTime) > timeout {
                            print("Timeout waiting for iCloud download")
                            return
                        }
                        
                        // Check download status
                        do {
                            let resourceValues = try url.resourceValues(forKeys: [.ubiquitousItemDownloadingStatusKey])
                            
                            if let status = resourceValues.ubiquitousItemDownloadingStatus {
                                print("Download status: \(status.rawValue)")
                                if status == .current {
                                    print("iCloud file download completed")
                                    break
                                } else if status == .notDownloaded && !downloadRequested {
                                    print("File not downloaded, requesting download...")
                                    try FileManager.default.startDownloadingUbiquitousItem(at: url)
                                    downloadRequested = true
                                } else if status == .notDownloaded && downloadRequested {
                                    print("Download request sent, waiting...")
                                }
                            } else {
                                // If we can't get status, try to access the file directly
                                if FileManager.default.fileExists(atPath: url.path) {
                                    print("iCloud file exists locally")
                                    break
                                }
                            }
                        } catch {
                            print("Error checking download status: \(error)")
                            // Fallback: check if file exists locally
                            if FileManager.default.fileExists(atPath: url.path) {
                                print("File exists locally (fallback check)")
                                break
                            }
                        }
                        
                        Thread.sleep(forTimeInterval: 1.0) // Increased to 1 second
                    }
                }
            } catch let error as NSError {
                print("Error downloading iCloud file: \(error)")
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
            print("Invalid file extension: \(ext)")
            return
        }
        
        // For iCloud files, trust the system's security model
        if url.path.contains("/Mobile Documents/") || url.path.contains("com~apple~CloudDocs") {
            print("Security: Allowing iCloud file access: \(url.path)")
        } else {
            // Use normal validation for non-iCloud files
            guard self.isValidDiskImageFile(url) else {
                print("Security: Invalid or unsafe file path: \(url.path)")
                return
            }
        }
        
        print("Security: File validation passed for: \(url.lastPathComponent)")
        
        // Continue with actual file loading - security scope already active
        // Call performFileLoad synchronously to maintain security scope
        self.performFileLoad(url: url)
    }
    
    private func performFileLoad(url: URL) {
        do {
            // File should be accessible now with security scope
            let imageData: Data = try Data(contentsOf: url)
            
            // Security: Validate file size (max 10MB for disk images)
            let maxSize = 10 * 1024 * 1024 // 10MB
            guard imageData.count <= maxSize else {
                print("Security: File too large: \(imageData.count) bytes")
                return
            }
            
            print("size:\(imageData.count)")
            
            // UI updates need to be on main thread
            DispatchQueue.main.async {
                let extname = url.pathExtension.removingPercentEncoding
                if extname?.lowercased() == "hdf" {
                    if let p = X68000_GetDiskImageBufferPointer(4, imageData.count) {
                        imageData.copyBytes(to: p, count: imageData.count)
                        X68000_LoadHDD(url.absoluteString)
                        // Small delay before reset to ensure stability
                        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                            X68000_Reset()
                            print("HDD loaded and reset complete")
                        }
                    }
                } else {
                    var drive = 0
                    if url.path.contains(" B.") || url.path.contains("_B.") {
                        drive = 1
                    }
                    
                    if let p = X68000_GetDiskImageBufferPointer(drive, imageData.count) {
                        imageData.copyBytes(to: p, count: imageData.count)
                        X68000_LoadFDD(drive, url.path)
                        if drive == 0 {
                            // Small delay before reset to ensure stability
                            DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                                X68000_Reset()
                                print("FDD loaded and reset complete (drive \(drive))")
                            }
                        } else {
                            print("FDD loaded successfully (drive \(drive))")
                        }
                    }
                }
            }
        } catch let error as NSError {
            print("Error loading file data: \(error)")
        }
    }
    func loadDiskImage( _ url : URL )
    {
        print("🐛 FileSystem.loadDiskImage called with: \(url.lastPathComponent)")
        print("🐛 Full path: \(url.path)")
        print("🐛 File extension: \(url.pathExtension)")
        saveSRAM()
        //        X68000_Reset()
        
        // Check for A/B disk pair functionality
        if shouldCheckForDiskPair(url) {
            print("🐛 A/B disk pair detected for: \(url.lastPathComponent)")
            // For File Provider Storage, we can only reliably access the file the user clicked
            // So we'll try to load as a pair, but fall back to single disk if companion fails
            if checkForCompanionDisk(url) {
                print("🐛 Companion disk might exist, attempting A/B pair load with fallback")
                loadDiskPairWithFallback(primaryUrl: url)
            } else {
                print("🐛 No companion disk found, loading single disk")
                loadAsynchronously( url )
            }
        } else {
            print("🐛 Single disk load for: \(url.lastPathComponent)")
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
            print("Debug: Assuming companion exists for cloud storage: \(companionUrl.lastPathComponent)")
            return true
        }
        
        // For local files, check if companion actually exists
        let companionExists = FileManager.default.fileExists(atPath: companionUrl.path)
        print("Debug: Companion disk check - looking for: \(companionUrl.lastPathComponent), exists: \(companionExists)")
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
        
        // Check if we're already loading this pair
        if let currentPair = FileSystem.currentlyLoadingPair, currentPair == pairIdentifier {
            print("Already loading disk pair: \(pairIdentifier), skipping")
            if let gameScene = self.gameScene {
                gameScene.clearLoadingFile(primaryUrl)
            }
            return
        }
        
        // Mark this pair as currently loading
        FileSystem.currentlyLoadingPair = pairIdentifier
        print("Debug: Set currently loading pair to: \(pairIdentifier)")
        
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
        
        print("A/B disk pair attempt:")
        print("A disk: \(aUrl.path)")
        print("B disk: \(bUrl.path)")
        print("Primary (clicked): \(primaryUrl.path)")
        
        // Try to load the primary disk first (the one user clicked)
        loadAsynchronously(primaryUrl)
        
        // Try to load the companion disk, but don't fail if it doesn't work
        let companionUrl = (primaryUrl.path == aUrl.path) ? bUrl : aUrl
        print("Debug: Attempting to load companion disk: \(companionUrl.lastPathComponent)")
        
        // Load companion with fallback - don't block if it fails
        DispatchQueue.global(qos: .background).asyncAfter(deadline: .now() + 0.5) { [weak self] in
            self?.loadCompanionDiskWithFallback(companionUrl: companionUrl) {
                // Cleanup regardless of success/failure
                FileSystem.currentlyLoadingPair = nil
                print("Debug: Cleared pair loading state for: \(pairIdentifier)")
            }
        }
    }
    
    // Try to load companion disk, but don't fail if access is denied
    private func loadCompanionDiskWithFallback(companionUrl: URL, completion: @escaping () -> Void) {
        print("Debug: Attempting companion disk load: \(companionUrl.lastPathComponent)")
        
        // Try to start security scope for companion
        guard companionUrl.startAccessingSecurityScopedResource() else {
            print("Info: Cannot access companion disk \(companionUrl.lastPathComponent) - user did not grant permission")
            completion()
            return
        }
        
        defer {
            companionUrl.stopAccessingSecurityScopedResource()
        }
        
        // Try to load companion disk
        do {
            let imageData = try Data(contentsOf: companionUrl)
            print("Debug: Successfully read companion disk: \(companionUrl.lastPathComponent)")
            
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
                    print("Success: Companion disk loaded: \(companionUrl.lastPathComponent) (drive \(drive))")
                } else {
                    print("Error: Failed to get buffer for companion disk")
                }
                completion()
            }
        } catch {
            print("Info: Could not load companion disk \(companionUrl.lastPathComponent): \(error)")
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
        
        print("Debug: Checking pair identifier: \(pairIdentifier)")
        print("Debug: Currently loading: \(FileSystem.currentlyLoadingPair ?? "none")")
        
        // Check if we're already loading this pair - only check at the pair level
        if let currentPair = FileSystem.currentlyLoadingPair, currentPair == pairIdentifier {
            print("Already loading disk pair: \(pairIdentifier), skipping")
            // Clean up the individual file tracking for this file since we're skipping the pair
            if let gameScene = self.gameScene {
                gameScene.clearLoadingFile(primaryUrl)
            }
            return
        }
        
        // Mark this pair as currently loading
        FileSystem.currentlyLoadingPair = pairIdentifier
        print("Debug: Set currently loading pair to: \(pairIdentifier)")
        
        // Create completion handler that clears the pair loading state
        let completePairLoading = {
            FileSystem.currentlyLoadingPair = nil
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
        
        print("A/B disk pair detected:")
        print("A disk: \(aUrl.path)")
        print("B disk: \(bUrl.path)")
        
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
                print("Neither A nor B disk found, loading single disk")
                loadAsynchronously(primaryUrl)
                completePairLoading()
            } else {
                loadMultipleDiskImages(existingUrls, completion: completePairLoading)
            }
        }
    }
    
    // Handle A/B disk pair loading with security scope (iCloud and File Provider Storage)
    private func loadDiskPairWithSecurityScope(aUrl: URL, bUrl: URL, originalUrl: URL, completion: @escaping () -> Void) {
        print("Debug: loadDiskPairWithSecurityScope called")
        print("Debug: A URL: \(aUrl.path)")
        print("Debug: B URL: \(bUrl.path)")
        print("Debug: Original URL (with security scope): \(originalUrl.path)")
        
        // Load both files with the same security scope session
        // We'll use a single security scope session to load both files
        loadDiskPairWithSharedSecurityScope(aUrl: aUrl, bUrl: bUrl, originalUrl: originalUrl, completion: completion)
    }
    
    // Load A/B disk pair with shared security scope
    private func loadDiskPairWithSharedSecurityScope(aUrl: URL, bUrl: URL, originalUrl: URL, completion: @escaping () -> Void) {
        print("Debug: loadDiskPairWithSharedSecurityScope called")
        print("Debug: Using original URL for security scope: \(originalUrl.lastPathComponent)")
        
        DispatchQueue.global(qos: .background).async { [weak self] in
            guard let self = self else {
                DispatchQueue.main.async { completion() }
                return
            }
            
            // Start security scope with the original URL (the one user clicked)
            guard originalUrl.startAccessingSecurityScopedResource() else {
                print("Error: Failed to start security scope with original URL: \(originalUrl.lastPathComponent)")
                DispatchQueue.main.async { completion() }
                return
            }
            
            defer {
                originalUrl.stopAccessingSecurityScopedResource()
                print("Debug: Released security scope for: \(originalUrl.lastPathComponent)")
            }
            
            // Now load both files within the same security scope
            var bSuccess = false
            var aSuccess = false
            let group = DispatchGroup()
            
            // Load B disk
            group.enter()
            self.loadDiskFileWithData(bUrl) { success in
                bSuccess = success
                if success {
                    print("B disk loaded successfully")
                } else {
                    print("B disk failed to load")
                }
                group.leave()
            }
            
            // Load A disk
            group.enter()
            self.loadDiskFileWithData(aUrl) { success in
                aSuccess = success
                if success {
                    print("A disk loaded successfully")
                } else {
                    print("A disk failed to load")
                }
                group.leave()
            }
            
            group.notify(queue: .main) {
                X68000_Reset()
                print("A/B disk pair loading completed - reset done")
                completion()
            }
        }
    }
    
    // Load disk file with data (assumes security scope is already active)
    private func loadDiskFileWithData(_ url: URL, completion: @escaping (Bool) -> Void) {
        print("Debug: loadDiskFileWithData called for: \(url.lastPathComponent)")
        
        // Validate file extension
        let validExtensions = ["dim", "xdf", "d88", "hdm", "hdf"]
        let ext = url.pathExtension.lowercased()
        guard validExtensions.contains(ext) else {
            print("Error: Invalid file extension: \(ext)")
            completion(false)
            return
        }
        
        do {
            print("Debug: Reading data from: \(url.path)")
            let imageData = try Data(contentsOf: url)
            print("Debug: Successfully read \(imageData.count) bytes from \(url.lastPathComponent)")
            
            // Determine drive based on filename
            var drive = 0
            let filename = url.deletingPathExtension().lastPathComponent.lowercased()
            if filename.hasSuffix("b") || filename.hasSuffix(" b") || filename.hasSuffix("_b") || 
               url.path.contains(" B.") || url.path.contains("_B.") {
                drive = 1
            }
            print("Debug: Determined drive: \(drive) for \(url.lastPathComponent)")
            
            DispatchQueue.main.async {
                let extname = url.pathExtension.lowercased()
                if extname == "hdf" {
                    print("Debug: Loading as HDD image")
                    if let p = X68000_GetDiskImageBufferPointer(4, imageData.count) {
                        imageData.copyBytes(to: p, count: imageData.count)
                        X68000_LoadHDD(url.absoluteString)
                        print("Success: HDD loaded: \(url.lastPathComponent)")
                        completion(true)
                    } else {
                        print("Error: Failed to get HDD buffer pointer")
                        completion(false)
                    }
                } else {
                    print("Debug: Loading as FDD image to drive \(drive)")
                    if let p = X68000_GetDiskImageBufferPointer(drive, imageData.count) {
                        imageData.copyBytes(to: p, count: imageData.count)
                        X68000_LoadFDD(drive, url.path)
                        print("Success: FDD loaded: \(url.lastPathComponent) (drive \(drive))")
                        completion(true)
                    } else {
                        print("Error: Failed to get FDD buffer pointer for drive \(drive)")
                        completion(false)
                    }
                }
            }
        } catch {
            print("Error: Failed to read data from \(url.lastPathComponent): \(error)")
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
        let isLastDisk = (index == urls.count - 1)
        let filename = url.deletingPathExtension().lastPathComponent.lowercased()
        let isADisk = filename.hasSuffix("a") || filename.hasSuffix(" a") || filename.hasSuffix("_a")
        
        loadDiskImageWithCallback(url) { [weak self] success in
            if success {
                print("Disk \(url.lastPathComponent) loaded successfully")
                
                // Reset only after A disk (drive 0) is loaded, or if it's the last disk
                if isADisk || isLastDisk {
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                        X68000_Reset()
                        print("Disk sequence loaded and reset complete")
                    }
                }
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
        print("Debug: loadDiskImageWithCallback called for: \(url.lastPathComponent)")
        DispatchQueue.global(qos: .background).async { [weak self] in
            print("Debug: Background queue started for: \(url.lastPathComponent)")
            guard let self = self else {
                print("Debug: Self is nil, completing with false")
                DispatchQueue.main.async { completion(false) }
                return
            }
            
            // Start security-scoped access for files that need it
            var needsSecurityScope = false
            if url.path.contains("/Mobile Documents/") || 
               url.path.contains("com~apple~CloudDocs") ||
               url.path.contains("/File Provider Storage/") {
                needsSecurityScope = true
                print("Debug: Starting security-scoped access for: \(url.path)")
                guard url.startAccessingSecurityScopedResource() else {
                    print("Error: Failed to start accessing security-scoped resource for: \(url.lastPathComponent)")
                    print("Error: Full path: \(url.path)")
                    DispatchQueue.main.async { completion(false) }
                    return
                }
                print("Debug: Security-scoped access started successfully for: \(url.lastPathComponent)")
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
        print("Debug: handleiCloudDownload called for: \(url.lastPathComponent)")
        do {
            let fileExists = FileManager.default.fileExists(atPath: url.path)
            print("Debug: File exists check: \(fileExists) for \(url.path)")
            
            if !fileExists {
                print("Debug: File doesn't exist, attempting to download from iCloud")
                try FileManager.default.startDownloadingUbiquitousItem(at: url)
                
                let timeout = 30.0
                let startTime = Date()
                
                while !FileManager.default.fileExists(atPath: url.path) {
                    if Date().timeIntervalSince(startTime) > timeout {
                        print("Error: Timeout downloading: \(url.lastPathComponent)")
                        completion(false)
                        return
                    }
                    Thread.sleep(forTimeInterval: 0.5)
                }
                print("Debug: Download completed for: \(url.lastPathComponent)")
            } else {
                print("Debug: File already exists locally: \(url.lastPathComponent)")
            }
            completion(true)
        } catch {
            print("Error downloading: \(url.lastPathComponent) - \(error)")
            completion(false)
        }
    }
    
    // Perform actual file loading
    private func performActualFileLoad(url: URL, completion: @escaping (Bool) -> Void) {
        print("Debug: performActualFileLoad called for: \(url.lastPathComponent)")
        
        // Validate file
        let validExtensions = ["dim", "xdf", "d88", "hdm", "hdf"]
        let ext = url.pathExtension.lowercased()
        guard validExtensions.contains(ext) else {
            print("Error: Invalid file extension: \(ext)")
            DispatchQueue.main.async { completion(false) }
            return
        }
        print("Debug: File extension validation passed: \(ext)")
        
        do {
            print("Debug: Attempting to read data from: \(url.path)")
            let imageData = try Data(contentsOf: url)
            print("Debug: Successfully read \(imageData.count) bytes from \(url.lastPathComponent)")
            
            // Determine drive based on filename
            var drive = 0
            let filename = url.deletingPathExtension().lastPathComponent.lowercased()
            if filename.hasSuffix("b") || filename.hasSuffix(" b") || filename.hasSuffix("_b") || 
               url.path.contains(" B.") || url.path.contains("_B.") {
                drive = 1
            }
            print("Debug: Determined drive: \(drive) for \(url.lastPathComponent)")
            
            DispatchQueue.main.async {
                let extname = url.pathExtension.lowercased()
                if extname == "hdf" {
                    print("Debug: Loading as HDD image")
                    if let p = X68000_GetDiskImageBufferPointer(4, imageData.count) {
                        imageData.copyBytes(to: p, count: imageData.count)
                        X68000_LoadHDD(url.absoluteString)
                        print("Success: HDD loaded: \(url.lastPathComponent)")
                        completion(true)
                    } else {
                        print("Error: Failed to get HDD buffer pointer")
                        completion(false)
                    }
                } else {
                    print("Debug: Loading as FDD image to drive \(drive)")
                    if let p = X68000_GetDiskImageBufferPointer(drive, imageData.count) {
                        imageData.copyBytes(to: p, count: imageData.count)
                        X68000_LoadFDD(drive, url.path)
                        print("Success: FDD loaded: \(url.lastPathComponent) (drive \(drive))")
                        completion(true)
                    } else {
                        print("Error: Failed to get FDD buffer pointer for drive \(drive)")
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
        print("==== Save SRAM ====")
        guard let sramPointer = X68000_GetSRAMPointer() else {
            print("Failed to get SRAM pointer")
            return
        }
        
        let data = Data(bytes: sramPointer, count: 0x400)
        guard let url = getDocumentsPath("SRAM.DAT") else {
            print("Failed to get SRAM file path")
            return
        }
        
        do {
            try data.write(to: url)
        } catch let error as NSError {
            print(error)
        }
    }
    
    func loadSRAM()
    {
        print("==== Load SRAM ====")
        guard let url = findFileInDocuments("SRAM.DAT") else {
            print("SRAM.DAT not found - starting with blank SRAM")
            return
        }
        
        do {
            let data: Data = try Data(contentsOf: url)
            // Security: Validate SRAM file size (should be exactly 0x400 bytes)
            guard data.count == 0x400 else {
                print("Security: Invalid SRAM file size: \(data.count), expected 0x400")
                return
            }
            if let p = X68000_GetSRAMPointer() {
                data.copyBytes(to: p, count: data.count)
                print("SRAM.DAT loaded successfully (\(data.count) bytes)")
            } else {
                print("Failed to get SRAM pointer")
            }
        } catch let error as NSError {
            print("Error loading SRAM.DAT: \(error)")
        }
    }
    // Added by Awed 2023/10/7
    func loadCGROM()
    {
        print("==== Load CGROM ====")
        guard let url = findFileInDocuments("CGROM.DAT") else {
            print("CGROM.DAT not found - will use embedded ROM")
            return
        }
        
        do {
            let data: Data = try Data(contentsOf: url)
            // Security: Validate CGROM file size (typical size is 0xc0000 bytes)
            guard data.count > 0 && data.count <= 0xc0000 else {
                print("Security: CGROM file invalid size: \(data.count)")
                return
            }
            if let p = X68000_GetCGROMPointer() {
                data.copyBytes(to: p, count: data.count)
                print("CGROM.DAT loaded successfully (\(data.count) bytes)")
            } else {
                print("Failed to get CGROM pointer")
            }
        } catch let error as NSError {
            print("Error loading CGROM.DAT: \(error)")
        }
    }
    
    func loadIPLROM()
    {
        print("==== Load IPLROM ====")
        guard let url = findFileInDocuments("IPLROM.DAT") else {
            print("IPLROM.DAT not found - will use embedded ROM")
            return
        }
        
        do {
            let data: Data = try Data(contentsOf: url)
            // Security: Validate IPLROM file size (typical size is 0x40000 bytes)
            guard data.count > 0 && data.count <= 0x40000 else {
                print("Security: IPLROM file invalid size: \(data.count)")
                return
            }
            if let p = X68000_GetIPLROMPointer() {
                data.copyBytes(to: p, count: data.count)
                print("IPLROM.DAT loaded successfully (\(data.count) bytes)")
            } else {
                print("Failed to get IPLROM pointer")
            }
        } catch let error as NSError {
            print("Error loading IPLROM.DAT: \(error)")
        }
    }
    
    // MARK: - Explicit FDD Drive Loading
    func loadFDDToDrive(_ url: URL, drive: Int) {
        print("🐛 FileSystem.loadFDDToDrive() called with: \(url.lastPathComponent) to drive \(drive)")
        
        do {
            let data = try Data(contentsOf: url)
            
            // Security: Validate file size for disk images
            guard data.count > 0 && data.count <= 2 * 1024 * 1024 else { // Max 2MB for floppy disk
                print("Security: Disk image file invalid size: \(data.count)")
                return
            }
            
            if let p = X68000_GetDiskImageBufferPointer(drive, data.count) {
                data.copyBytes(to: p, count: data.count)
                X68000_LoadFDD(drive, url.path)
                print("Success: FDD loaded: \(url.lastPathComponent) to drive \(drive)")
                
                // Reset only if drive A is loaded
                if drive == 0 {
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                        X68000_Reset()
                        print("FDD loaded and reset complete (drive \(drive))")
                    }
                }
            } else {
                print("Error: Failed to get FDD buffer pointer for drive \(drive)")
            }
        } catch let error as NSError {
            print("Error loading FDD image: \(error)")
        }
    }
    
    // end
    
    
}
