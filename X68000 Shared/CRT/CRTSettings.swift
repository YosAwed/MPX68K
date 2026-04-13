//
//  CRTSettings.swift
//  X68000 Shared
//
//  Lightweight configuration for CRT-style rendering effects.
//

import Foundation

public struct CRTSettings: Codable, Equatable {
    // Intensities are in 0.0 ... 1.0 unless noted
    public var enabled: Bool
    public var scanlineIntensity: Float    // 0.0 - 1.0
    public var scanlinePitch: Float        // in pixels (vertical)
    public var curvature: Float            // 0.0 - 0.5 typical
    public var phosphorPersistence: Float  // 0.0 - 1.0 (mix with previous frame)
    public var chromaShiftR: Float         // pixels at edge (positive shift outward)
    public var chromaShiftB: Float         // pixels at edge (negative shift inward)
    public var noiseIntensity: Float       // 0.0 - 1.0
    public var vignetteIntensity: Float    // 0.0 - 1.0
    public var bloomIntensity: Float       // reserved (shader approximates lightweight)

    public init(
        enabled: Bool = false,
        scanlineIntensity: Float = 0.35,
        scanlinePitch: Float = 1.0,
        curvature: Float = 0.10,
        phosphorPersistence: Float = 0.20,
        chromaShiftR: Float = 0.8,
        chromaShiftB: Float = -0.8,
        noiseIntensity: Float = 0.03,
        vignetteIntensity: Float = 0.20,
        bloomIntensity: Float = 0.0
    ) {
        self.enabled = enabled
        self.scanlineIntensity = scanlineIntensity
        self.scanlinePitch = scanlinePitch
        self.curvature = curvature
        self.phosphorPersistence = phosphorPersistence
        self.chromaShiftR = chromaShiftR
        self.chromaShiftB = chromaShiftB
        self.noiseIntensity = noiseIntensity
        self.vignetteIntensity = vignetteIntensity
        self.bloomIntensity = bloomIntensity
    }
}

public enum CRTPreset: String, CaseIterable, Codable {
    case off
    case subtle
    case standard
    case enhanced
    case custom
}

public struct CRTPresets {
    public static func settings(for preset: CRTPreset) -> CRTSettings {
        switch preset {
        case .off:
            return CRTSettings(enabled: false,
                               scanlineIntensity: 0.0,
                               scanlinePitch: 1.0,
                               curvature: 0.0,
                               phosphorPersistence: 0.0,
                               chromaShiftR: 0.0,
                               chromaShiftB: 0.0,
                               noiseIntensity: 0.0,
                               vignetteIntensity: 0.0,
                               bloomIntensity: 0.0)
        case .subtle:
            return CRTSettings(enabled: true,
                               scanlineIntensity: 0.20,
                               scanlinePitch: 1.0,
                               curvature: 0.06,
                               phosphorPersistence: 0.10,
                               chromaShiftR: 0.4,
                               chromaShiftB: -0.4,
                               noiseIntensity: 0.02,
                               vignetteIntensity: 0.10,
                               bloomIntensity: 0.10)
        case .standard:
            return CRTSettings(enabled: true,
                               scanlineIntensity: 0.60,
                               scanlinePitch: 1.0,
                               curvature: 0.15,
                               phosphorPersistence: 0.25,
                               chromaShiftR: 1.2,
                               chromaShiftB: -1.2,
                               noiseIntensity: 0.05,
                               vignetteIntensity: 0.25,
                               bloomIntensity: 0.25)
        case .enhanced:
            return CRTSettings(enabled: true,
                               scanlineIntensity: 0.80,
                               scanlinePitch: 1.0,
                               curvature: 0.27,
                               phosphorPersistence: 0.45,
                               chromaShiftR: 1.9,
                               chromaShiftB: -1.9,
                               noiseIntensity: 0.08,
                               vignetteIntensity: 0.35,
                               bloomIntensity: 0.35)
        case .custom:
            // Caller should supply explicit settings; this is just a sane default
            return CRTSettings()
        }
    }
}
