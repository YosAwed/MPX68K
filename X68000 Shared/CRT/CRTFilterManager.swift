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

    // MARK: - Public API
    func attach(to node: SKSpriteNode) {
        self.node = node
        if settings.enabled {
            ensureShader()
            node.shader = shader
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
        if settings.enabled {
            ensureShader()
            node.shader = shader
            applySettingsToUniforms()
        } else {
            node.shader = nil
        }
    }

    func frameDidUpdate(with currentTexture: SKTexture, resolution: vector_float2, dt: Float) {
        guard settings.enabled else { return }
        ensureShader()
        time += max(0.0, dt)
        setFloat("u_time", time)
        setVec2("u_resolution", resolution)

        // Persistence: feed previous frame texture
        if settings.phosphorPersistence > 0.001 {
            if let prev = prevTexture {
                setTexture("u_prevTex", prev)
                setFloat("u_persistence", settings.phosphorPersistence)
            } else {
                // First frame without history: disable persistence blend for this frame
                setFloat("u_persistence", 0.0)
            }
            prevTexture = currentTexture // keep only one backbuffer
        } else {
            setTexture("u_prevTex", nil)
            setFloat("u_persistence", 0.0)
            prevTexture = nil
        }
    }

    // MARK: - Shader and uniforms
    private func ensureShader() {
        if shader != nil { return }
        let source = CRTFilterManager.crtFragmentSource
        let s = SKShader(source: source)

        // Bootstrap uniforms with defaults
        let initialUniforms: [SKUniform] = [
            SKUniform(name: "u_time", float: 0.0),
            SKUniform(name: "u_resolution", vectorFloat2: vector_float2(320, 240)),
            SKUniform(name: "u_scanlineIntensity", float: settings.scanlineIntensity),
            SKUniform(name: "u_scanlinePitch", float: max(1.0, settings.scanlinePitch)),
            SKUniform(name: "u_curvature", float: settings.curvature),
            SKUniform(name: "u_chromaShiftR", float: settings.chromaShiftR),
            SKUniform(name: "u_chromaShiftB", float: settings.chromaShiftB),
            SKUniform(name: "u_noiseIntensity", float: settings.noiseIntensity),
            SKUniform(name: "u_vignetteIntensity", float: settings.vignetteIntensity),
            SKUniform(name: "u_persistence", float: settings.phosphorPersistence),
            SKUniform(name: "u_bloomIntensity", float: settings.bloomIntensity)
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
        setFloat("u_curvature", settings.curvature)
        setFloat("u_chromaShiftR", settings.chromaShiftR)
        setFloat("u_chromaShiftB", settings.chromaShiftB)
        setFloat("u_noiseIntensity", settings.noiseIntensity)
        setFloat("u_vignetteIntensity", settings.vignetteIntensity)
        setFloat("u_persistence", settings.phosphorPersistence)
        setFloat("u_bloomIntensity", settings.bloomIntensity)
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
    // Single-pass fragment shader approximating CRT effects.
    // SpriteKit supplies: varying vec2 v_tex_coord; uniform sampler2D u_texture;
    static let crtFragmentSource = """
    // Minimal, macOS-friendly SKShader fragment to ensure visible effect
    varying vec2 v_tex_coord;
    uniform sampler2D u_texture;

    uniform float u_time;
    uniform vec2  u_resolution;      // (width, height) in pixels
    uniform float u_scanlineIntensity;
    uniform float u_scanlinePitch;   // in pixels
    uniform float u_noiseIntensity;
    uniform float u_vignetteIntensity;

    float rand(vec2 co){
        return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
    }

    void main() {
        vec2 uv = v_tex_coord;
        vec3 color = texture2D(u_texture, uv).rgb;

        // 1) Strong scanlines for visibility
        float row = uv.y * u_resolution.y;
        float rowIdx = floor(row / max(1.0, u_scanlinePitch));
        float odd = mod(rowIdx, 2.0);
        float scan = mix(1.0, 1.0 - u_scanlineIntensity, odd);
        color *= scan;

        // 2) Noise
        if (u_noiseIntensity > 0.001) {
            float n = rand(uv + vec2(u_time * 0.123, u_time * 0.234));
            color += (n - 0.5) * u_noiseIntensity;
        }

        // 3) Vignette (extremely subtle; edge-only handled via Core Image)
        if (u_vignetteIntensity > 0.001) {
            float r = length(uv - 0.5) / 0.7071; // 0..~1
            float vig = 1.0 - (u_vignetteIntensity * 0.05) * smoothstep(0.8, 1.0, r);
            color *= vig;
        }

        gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
    }
    """
}
