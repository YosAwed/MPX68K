//
//  CRTFilterManager.swift
//  X68000 Shared
//
//  Manages a single-pass SKShader that approximates CRT display traits
//  (scanlines, curvature, chromatic aberration, vignette, noise, light persistence).
//

import Foundation
import SpriteKit
import simd

final class CRTFilterManager {
    private weak var node: SKSpriteNode?
    private var shader: SKShader?
    private var prevTexture: SKTexture?
    private var time: Float = 0.0
    private var uniforms: [String: SKUniform] = [:]
    private(set) var preset: CRTPreset = .off
    private(set) var settings: CRTSettings = CRTPresets.settings(for: .off)
    // Superimpose (luma key) uniforms
    private var superEnabled: Float = 0.0
    private var superThreshold: Float = 0.03
    private var superSoftness: Float = 0.04
    private var superAlpha: Float = 1.0

    // MARK: - Public API
    func attach(to node: SKSpriteNode) {
        self.node = node
        if settings.enabled || superEnabled > 0.0 {
            ensureShader()
            node.shader = shader
            // Re-apply superimpose uniforms after shader creation
            applySuperimposeUniformsToShader()
        } else {
            node.shader = nil
        }
    }

    func detach() {
        node?.shader = nil
        shader = nil
        uniforms.removeAll()
        prevTexture = nil
        time = 0.0
    }

    func apply(preset: CRTPreset) {
        self.preset = preset
        let newSettings = CRTPresets.settings(for: preset)
        update(settings: newSettings)
    }

    func update(settings: CRTSettings) {
        self.settings = settings
        guard let node = node else { return }
        if settings.enabled || superEnabled > 0.0 {
            ensureShader()
            node.shader = shader
            applySettingsToUniforms()
            // Re-apply superimpose uniforms after shader update
            applySuperimposeUniformsToShader()
        } else {
            node.shader = nil
        }
    }

    func frameDidUpdate(with currentTexture: SKTexture, resolution: vector_float2, dt: Float) {
        guard settings.enabled else { return }
        ensureShader()
        // No per-frame uniforms needed for basic transparency test
    }

    // MARK: - Shader and uniforms
    private func ensureShader() {
        if shader != nil { return }
        let source = CRTFilterManager.crtFragmentSource
        let s = SKShader(source: source)

        // Bootstrap uniforms - minimal set for transparency shader
        let initialUniforms: [SKUniform] = [
            SKUniform(name: "u_superEnabled", float: superEnabled),
            SKUniform(name: "u_superThreshold", float: superThreshold),
            SKUniform(name: "u_superSoftness", float: superSoftness),
            SKUniform(name: "u_superAlpha", float: superAlpha),
        ]

        s.uniforms = initialUniforms
        self.shader = s
        // Build local lookup
        uniforms.removeAll()
        for u in initialUniforms { uniforms[u.name] = u }
    }

    private func applySettingsToUniforms() {
        setFloat("u_scanlineIntensity", settings.scanlineIntensity)
        setFloat("u_scanlinePitch", max(1.0, settings.scanlinePitch))
        setFloat("u_noiseIntensity", settings.noiseIntensity)
        setFloat("u_vignetteIntensity", settings.vignetteIntensity)
    }

    // Public: update superimpose uniforms
    func setSuperimpose(enabled: Bool, threshold: Float, softness: Float, alpha: Float) {
        let newEnabled: Float = enabled ? 1.0 : 0.0

        // Only log when values change significantly
        let significantChange = fabsf(newEnabled - superEnabled) > 0.1 ||
                               fabsf(threshold - superThreshold) > 0.001 ||
                               fabsf(softness - superSoftness) > 0.001 ||
                               fabsf(alpha - superAlpha) > 0.001

        if significantChange {
            print("DEBUG: CRTFilter setSuperimpose - enabled: \(newEnabled), threshold: \(threshold), softness: \(softness), alpha: \(alpha)")
        }

        superEnabled = newEnabled
        superThreshold = threshold
        superSoftness = softness
        superAlpha = alpha
        setFloat("u_superEnabled", superEnabled)
        setFloat("u_superThreshold", superThreshold)
        setFloat("u_superSoftness", superSoftness)
        setFloat("u_superAlpha", superAlpha)
    }


    // Public access to shader for superimpose mode
    func getShader() -> SKShader? {
        ensureShader()
        return shader
    }

    // Re-apply current superimpose uniforms to the shader (used after shader recreate)
    private func applySuperimposeUniformsToShader() {
        ensureShader()
        setFloat("u_superEnabled", superEnabled)
        setFloat("u_superThreshold", superThreshold)
        setFloat("u_superSoftness", superSoftness)
        setFloat("u_superAlpha", superAlpha)
    }

    private func setFloat(_ name: String, _ value: Float) {
        if let u = uniforms[name] { u.floatValue = value; return }
        let u = SKUniform(name: name, float: value)
        uniforms[name] = u
        shader?.addUniform(u)
    }

    private func setVec2(_ name: String, _ value: vector_float2) {
        if let u = uniforms[name] { u.vectorFloat2Value = value; return }
        let u = SKUniform(name: name, vectorFloat2: value)
        uniforms[name] = u
        shader?.addUniform(u)
    }

    private func setTexture(_ name: String, _ value: SKTexture?) {
        if let u = uniforms[name] {
            u.textureValue = value
            return
        }
        let u = SKUniform(name: name, texture: value)
        uniforms[name] = u
        shader?.addUniform(u)
    }

    // MARK: - Shader source
    // Luma-key transparency shader with smooth transitions - SpriteKit compatible
    static let crtFragmentSource = """
    void main() {
        vec4 color = texture2D(u_texture, v_tex_coord);

        // Luma-key based transparency
        float a = 1.0;
        if (u_superEnabled >= 0.5) {
            float luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
            float k = smoothstep(u_superThreshold, u_superThreshold + u_superSoftness, luma);
            a = clamp(u_superAlpha, 0.0, 1.0) * k;
        }

        gl_FragColor = vec4(color.rgb, a);
    }
    """
}
