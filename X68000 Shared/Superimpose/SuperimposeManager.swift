//
//  SuperimposeManager.swift
//  Handles background video (superimpose) playback and settings
//

import Foundation
import SpriteKit
import AVFoundation
import YouTubeKit

enum SuperimposeError: Error {
    case noPlayableStream
}

final class SuperimposeManager: NSObject {
    struct Settings: Codable, Equatable {
        var enabled: Bool = false
        var threshold: Float = 0.03
        var softness: Float = 0.04
        var alpha: Float = 1.0
        var videoScale: Float = 0.8  // Scale factor for video (0.8 = 80% of emulator screen)
        var bookmarkedURLData: Data? = nil
    }

    private(set) var settings = Settings()

    private var player: AVPlayer?
    private var queuePlayer: AVQueuePlayer?
    private var looper: AVPlayerLooper?
    private(set) var videoNode: SKVideoNode?
    private weak var scene: SKScene?

    func attach(to scene: SKScene) {
        errorLog("SuperimposeManager: attach to scene called", category: .ui)
        self.scene = scene
        if let node = videoNode, node.parent == nil {
            errorLog("SuperimposeManager: Adding existing video node to scene", category: .ui)
            node.zPosition = -10
            node.position = .zero
            scene.addChild(node)
            adjustVideoScale(matching: nil)
        } else {
            errorLog("SuperimposeManager: No existing video node to attach", category: .ui)
        }
    }

    func loadVideo(url: URL) throws {
        print("DEBUG: SuperimposeManager.loadVideo called")
        errorLog("Loading background video: \(url.lastPathComponent)", category: .ui)

        // Use simplified direct loading for better reliability
        loadVideoDirect(url: url)
        print("DEBUG: loadVideoDirect called, returning from loadVideo")
    }

    // Fallback simple loader using SKVideoNode(url:) with proper setup
    func loadVideoDirect(url: URL) {
        errorLog("loadVideoDirect called for: \(url.lastPathComponent)", category: .ui)

        DispatchQueue.main.async {
            errorLog("Creating AVPlayer for video", category: .ui)

            // Clean up existing resources
            self.removeVideo()

            // Start accessing security-scoped resource (for sandboxed macOS app)
            var needsStopAccessing = false
            if url.startAccessingSecurityScopedResource() {
                needsStopAccessing = true
                errorLog("Started accessing security-scoped resource", category: .ui)
            }

            // Create AVPlayer for proper control
            let player = AVPlayer(url: url)
            player.isMuted = true

            // Setup error handling
            if let currentItem = player.currentItem {
                NotificationCenter.default.addObserver(
                    forName: .AVPlayerItemFailedToPlayToEndTime,
                    object: currentItem,
                    queue: .main
                ) { notification in
                    if let error = notification.userInfo?[AVPlayerItemFailedToPlayToEndTimeErrorKey] as? Error {
                        errorLog("Video playback failed: \(error)", category: .ui)
                    }
                }

                NotificationCenter.default.addObserver(
                    forName: .AVPlayerItemNewErrorLogEntry,
                    object: currentItem,
                    queue: .main
                ) { notification in
                    errorLog("Video error log entry", category: .ui)
                }
            }

            // Setup looping
            NotificationCenter.default.addObserver(
                forName: .AVPlayerItemDidPlayToEndTime,
                object: player.currentItem,
                queue: .main
            ) { _ in
                errorLog("Video loop: seeking to beginning", category: .ui)
                player.seek(to: .zero)
                player.play()
            }

            // Monitor player status first
            player.addObserver(self, forKeyPath: "status", options: [.new], context: nil)

            // Wait for player to be ready before creating video node
            if player.status == .readyToPlay {
                self.createAndAddVideoNode(player: player)
            } else {
                // Player will create video node when ready (via KVO)
                self.player = player
                errorLog("Waiting for player to be ready...", category: .ui)
            }

            // Stop accessing security-scoped resource after a delay
            if needsStopAccessing {
                DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) {
                    url.stopAccessingSecurityScopedResource()
                    errorLog("Stopped accessing security-scoped resource", category: .ui)
                }
            }
        }
    }

    func loadVideoFromYouTube(url: URL) async throws {
        errorLog("Loading YouTube video: \(url.absoluteString)", category: .ui)

        let youtube = YouTube(url: url, methods: [.local, .remote])
        let streams = try await youtube.streams

        guard let stream = streams
            .filterVideoAndAudio()
            .filter({ $0.isNativelyPlayable })
            .highestResolutionStream() else {
            throw SuperimposeError.noPlayableStream
        }

        errorLog("Selected stream: \(stream.url.absoluteString)", category: .ui)

        try loadVideo(url: stream.url)
    }

    func removeVideo() {
        // Remove KVO observer if exists
        if let player = player {
            player.removeObserver(self, forKeyPath: "status")
        }

        NotificationCenter.default.removeObserver(self)
        player?.pause()
        videoNode?.removeFromParent()
        videoNode = nil
        player = nil
        queuePlayer = nil
        looper = nil
    }

    private func createAndAddVideoNode(player: AVPlayer) {
        DispatchQueue.main.async {
            errorLog("Creating video node for background playbook", category: .ui)

            // Create actual video node
            let videoNode = SKVideoNode(avPlayer: player)
            videoNode.zPosition = -10
            videoNode.position = .zero

            if let scene = self.scene {
                scene.addChild(videoNode)
                videoNode.play()
                errorLog("Added video node to scene and started playback", category: .ui)

                // Initial scaling - will be adjusted later when sprite is available
                self.adjustVideoScale(matching: nil)
                errorLog("Initial video scaling applied", category: .ui)
            } else {
                errorLog("WARNING: No scene attached for video node", category: .ui)
            }

            self.videoNode = videoNode
        }
    }

    // KVO observer for player status
    override func observeValue(forKeyPath keyPath: String?, of object: Any?, change: [NSKeyValueChangeKey : Any]?, context: UnsafeMutableRawPointer?) {
        if keyPath == "status" {
            if let player = object as? AVPlayer {
                switch player.status {
                case .readyToPlay:
                    errorLog("Video player ready to play", category: .ui)
                    if videoNode == nil {
                        createAndAddVideoNode(player: player)
                    }
                case .failed:
                    if let error = player.error {
                        errorLog("Video player failed: \(error)", category: .ui)
                    }
                case .unknown:
                    errorLog("Video player status unknown", category: .ui)
                @unknown default:
                    errorLog("Video player status unknown default", category: .ui)
                }
            }
        }
    }

    func play() {
        if let node = videoNode {
            node.play()
            errorLog("SKVideoNode play() called", category: .ui)
        } else {
            player?.play()
            errorLog("AVPlayer play() called", category: .ui)
        }
    }

    func pause() {
        if let node = videoNode {
            node.pause()
            errorLog("SKVideoNode pause() called", category: .ui)
        } else {
            player?.pause()
            errorLog("AVPlayer pause() called", category: .ui)
        }
    }

    func setEnabled(_ on: Bool) { settings.enabled = on }
    func setThreshold(_ v: Float) { settings.threshold = max(0, min(0.2, v)) }
    func setSoftness(_ v: Float) { settings.softness = max(0, min(0.2, v)) }
    func setAlpha(_ v: Float) { settings.alpha = max(0, min(1, v)) }

    func adjustVideoScale(matching sprite: SKSpriteNode?) {
        guard let node = videoNode, let scene = scene else { return }

        if let sp = sprite {
            // Match sprite transform but apply scale factor to make video smaller
            node.position = sp.position
            node.zRotation = sp.zRotation

            // Apply scale factor to the sprite size
            let scaledWidth = sp.size.width * CGFloat(settings.videoScale)
            let scaledHeight = sp.size.height * CGFloat(settings.videoScale)
            node.size = CGSize(width: scaledWidth, height: scaledHeight)

            node.xScale = sp.xScale
            node.yScale = sp.yScale
            errorLog("Video scaled to \(Int(settings.videoScale * 100))% of sprite - size: \(node.size), position: \(sp.position)", category: .ui)
        } else {
            // Fallback: use a reasonable emulator screen size (4:3 aspect ratio)
            let emulatorWidth: CGFloat = 640
            let emulatorHeight: CGFloat = 480
            let aspectRatio = emulatorWidth / emulatorHeight

            // Scale to fit within scene while maintaining aspect ratio
            let sceneAspect = scene.size.width / scene.size.height
            var scaledSize: CGSize

            if aspectRatio > sceneAspect {
                // Video is wider, fit to scene width
                scaledSize = CGSize(width: scene.size.width, height: scene.size.width / aspectRatio)
            } else {
                // Video is taller, fit to scene height
                scaledSize = CGSize(width: scene.size.height * aspectRatio, height: scene.size.height)
            }

            // Apply scale factor to reduce size
            scaledSize = CGSize(
                width: scaledSize.width * CGFloat(settings.videoScale),
                height: scaledSize.height * CGFloat(settings.videoScale)
            )

            node.size = scaledSize
            node.position = .zero
            node.xScale = 1.0
            node.yScale = 1.0
            errorLog("Video scaled to \(Int(settings.videoScale * 100))% of emulator aspect ratio - size: \(scaledSize)", category: .ui)
        }
    }
}
