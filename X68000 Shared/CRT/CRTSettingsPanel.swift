//
//  CRTSettingsPanel.swift
//  Modern SwiftUI-based CRT settings panel with macOS design language
//

import SwiftUI

struct CRTSettingsPanel: View {
    @Environment(\.dismiss) private var dismiss

    @State private var scanlineIntensity: Double
    @State private var curvature: Double
    @State private var chromatic: Double
    @State private var persistence: Double
    @State private var vignette: Double
    @State private var bloom: Double
    @State private var noise: Double

    @State private var selectedPreset: CRTPreset

    let initialSettings: CRTSettings
    let onSettingsChanged: (CRTSettings) -> Void
    let onPresetChanged: (CRTPreset) -> Void

    init(settings: CRTSettings, preset: CRTPreset, onSettingsChanged: @escaping (CRTSettings) -> Void, onPresetChanged: @escaping (CRTPreset) -> Void) {
        self.initialSettings = settings
        self.onSettingsChanged = onSettingsChanged
        self.onPresetChanged = onPresetChanged

        _scanlineIntensity = State(initialValue: Double(settings.scanlineIntensity))
        _curvature = State(initialValue: Double(settings.curvature))
        _chromatic = State(initialValue: Double(max(abs(settings.chromaShiftR), abs(settings.chromaShiftB))))
        _persistence = State(initialValue: Double(settings.phosphorPersistence))
        _vignette = State(initialValue: Double(settings.vignetteIntensity))
        _bloom = State(initialValue: Double(settings.bloomIntensity))
        _noise = State(initialValue: Double(settings.noiseIntensity))
        _selectedPreset = State(initialValue: preset)
    }

    var body: some View {
        VStack(spacing: 0) {
            // Header
            HStack {
                Text("CRT Display Settings")
                    .font(.system(size: 20, weight: .semibold))
                    .foregroundColor(.primary)

                Spacer()

                Button(action: { dismiss() }) {
                    Image(systemName: "xmark.circle.fill")
                        .font(.system(size: 20))
                        .foregroundStyle(.secondary)
                        .symbolRenderingMode(.hierarchical)
                }
                .buttonStyle(.plain)
                .help("Close settings")
            }
            .padding(.horizontal, 24)
            .padding(.vertical, 20)

            Divider()

            ScrollView {
                VStack(spacing: 20) {
                    // Presets section
                    VStack(alignment: .leading, spacing: 12) {
                        Text("Presets")
                            .font(.system(size: 13, weight: .semibold))
                            .foregroundColor(.secondary)
                            .textCase(.uppercase)

                        HStack(spacing: 8) {
                            ForEach([CRTPreset.off, .subtle, .standard, .enhanced], id: \.self) { preset in
                                PresetButton(
                                    title: preset.displayName,
                                    isSelected: selectedPreset == preset,
                                    action: {
                                        selectedPreset = preset
                                        applyPreset(preset)
                                    }
                                )
                            }
                        }
                    }
                    .padding(.horizontal, 24)
                    .padding(.top, 16)

                    Divider()
                        .padding(.horizontal, 24)

                    // Custom adjustments section
                    VStack(alignment: .leading, spacing: 16) {
                        Text("Fine Tuning")
                            .font(.system(size: 13, weight: .semibold))
                            .foregroundColor(.secondary)
                            .textCase(.uppercase)

                        VStack(spacing: 20) {
                            SettingSlider(
                                title: "Scanlines",
                                value: $scanlineIntensity,
                                range: 0.0...1.0,
                                onChange: settingsDidChange
                            )

                            SettingSlider(
                                title: "Screen Curvature",
                                value: $curvature,
                                range: 0.0...0.4,
                                onChange: settingsDidChange
                            )

                            SettingSlider(
                                title: "Chromatic Aberration",
                                value: $chromatic,
                                range: 0.0...2.5,
                                unit: "px",
                                onChange: settingsDidChange
                            )

                            SettingSlider(
                                title: "Phosphor Persistence",
                                value: $persistence,
                                range: 0.0...0.8,
                                onChange: settingsDidChange
                            )

                            SettingSlider(
                                title: "Vignette",
                                value: $vignette,
                                range: 0.0...0.8,
                                onChange: settingsDidChange
                            )

                            SettingSlider(
                                title: "Bloom",
                                value: $bloom,
                                range: 0.0...0.6,
                                onChange: settingsDidChange
                            )

                            SettingSlider(
                                title: "Noise",
                                value: $noise,
                                range: 0.0...0.2,
                                onChange: settingsDidChange
                            )
                        }
                    }
                    .padding(.horizontal, 24)
                    .padding(.bottom, 20)
                }
            }
        }
        .frame(width: 500, height: 600)
        .background(Color(nsColor: .windowBackgroundColor))
    }

    private func applyPreset(_ preset: CRTPreset) {
        let settings = CRTPresets.settings(for: preset)

        withAnimation(.easeInOut(duration: 0.2)) {
            scanlineIntensity = Double(settings.scanlineIntensity)
            curvature = Double(settings.curvature)
            chromatic = Double(max(abs(settings.chromaShiftR), abs(settings.chromaShiftB)))
            persistence = Double(settings.phosphorPersistence)
            vignette = Double(settings.vignetteIntensity)
            bloom = Double(settings.bloomIntensity)
            noise = Double(settings.noiseIntensity)
        }

        onPresetChanged(preset)
    }

    private func settingsDidChange() {
        // Mark as custom when manually adjusted
        if selectedPreset != .custom {
            selectedPreset = .custom
        }

        let settings = CRTSettings(
            enabled: true,
            scanlineIntensity: Float(scanlineIntensity),
            scanlinePitch: 1.0,
            curvature: Float(curvature),
            phosphorPersistence: Float(persistence),
            chromaShiftR: Float(chromatic),
            chromaShiftB: -Float(chromatic),
            noiseIntensity: Float(noise),
            vignetteIntensity: Float(vignette),
            bloomIntensity: Float(bloom)
        )

        onSettingsChanged(settings)
    }
}

// MARK: - Preset Button Component
struct PresetButton: View {
    let title: String
    let isSelected: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 13, weight: isSelected ? .semibold : .regular))
                .foregroundColor(isSelected ? .white : .primary)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 8)
                .background(
                    RoundedRectangle(cornerRadius: 6)
                        .fill(isSelected ? Color.accentColor : Color(nsColor: .controlBackgroundColor))
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(Color(nsColor: .separatorColor), lineWidth: isSelected ? 0 : 1)
                )
        }
        .buttonStyle(.plain)
    }
}

// MARK: - Setting Slider Component
struct SettingSlider: View {
    let title: String
    @Binding var value: Double
    let range: ClosedRange<Double>
    var unit: String = ""
    let onChange: () -> Void

    @State private var isEditing = false

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(title)
                    .font(.system(size: 13, weight: .medium))
                    .foregroundColor(.primary)

                Spacer()

                Text(formattedValue)
                    .font(.system(size: 12, weight: .regular))
                    .foregroundColor(.secondary)
                    .monospacedDigit()
            }

            Slider(
                value: $value,
                in: range,
                onEditingChanged: { editing in
                    isEditing = editing
                    if !editing {
                        onChange()
                    }
                }
            )
            .tint(.accentColor)
        }
    }

    private var formattedValue: String {
        if unit.isEmpty {
            return String(format: "%.2f", value)
        } else {
            return String(format: "%.2f %@", value, unit)
        }
    }
}

// MARK: - CRTPreset Extension
extension CRTPreset {
    var displayName: String {
        switch self {
        case .off: return "Off"
        case .subtle: return "Subtle"
        case .standard: return "Standard"
        case .enhanced: return "Enhanced"
        case .custom: return "Custom"
        }
    }
}

// MARK: - Preview Provider
#if DEBUG
struct CRTSettingsPanel_Previews: PreviewProvider {
    static var previews: some View {
        CRTSettingsPanel(
            settings: CRTPresets.settings(for: .standard),
            preset: .standard,
            onSettingsChanged: { _ in },
            onPresetChanged: { _ in }
        )
    }
}
#endif
