//
//  CRTOverlay.swift
//  Simple SpriteKit-based overlay with sliders to tweak CRT settings.
//

import SpriteKit

final class CRTOverlay: SKNode {
    struct Item {
        let key: String
        let title: String
        let range: ClosedRange<Float>
        var value: Float
        let formatter: (Float) -> String
    }

    private var items: [Item] = []
    private var trackNodes: [String: SKShapeNode] = [:]
    private var knobNodes: [String: SKShapeNode] = [:]
    private var valueLabels: [String: SKLabelNode] = [:]
    private var background: SKShapeNode!
    private let panelWidth: CGFloat = 560
    private let panelHeight: CGFloat = 360
    private let margin: CGFloat = 20
    private let trackWidth: CGFloat = 360
    private let trackHeight: CGFloat = 6
    private let knobRadius: CGFloat = 10

    var onValueChanged: ((String, Float) -> Void)?
    var onClose: (() -> Void)?

    override init() {
        super.init()
        isUserInteractionEnabled = true
        zPosition = 1000
    }

    required init?(coder aDecoder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func configure(with settings: CRTSettings) {
        removeAllChildren()
        trackNodes.removeAll(); knobNodes.removeAll(); valueLabels.removeAll()

        background = SKShapeNode(rectOf: CGSize(width: panelWidth, height: panelHeight), cornerRadius: 12)
        background.fillColor = SKColor(white: 0.05, alpha: 0.92)
        background.strokeColor = SKColor(white: 1.0, alpha: 0.15)
        addChild(background)

        let title = SKLabelNode(text: "CRT Settings")
        title.fontSize = 22
        title.fontColor = .white
        title.position = CGPoint(x: 0, y: panelHeight/2 - 40)
        title.zPosition = 1
        addChild(title)

        let closeBtn = SKLabelNode(text: "✕ Close")
        closeBtn.name = "btn-close"
        closeBtn.fontSize = 16
        closeBtn.fontColor = .white
        closeBtn.position = CGPoint(x: panelWidth/2 - 70, y: panelHeight/2 - 40)
        closeBtn.zPosition = 1
        addChild(closeBtn)

        // Define sliders
        items = [
            Item(key: "scanlineIntensity", title: "Scanlines", range: 0.0...1.0, value: settings.scanlineIntensity, formatter: { String(format: "%.2f", $0) }),
            Item(key: "curvature", title: "Curvature", range: 0.0...0.4, value: settings.curvature, formatter: { String(format: "%.2f", $0) }),
            Item(key: "chroma", title: "Chromatic", range: 0.0...2.5, value: max(abs(settings.chromaShiftR), abs(settings.chromaShiftB)), formatter: { String(format: "%.2f px", $0) }),
            Item(key: "persistence", title: "Persistence", range: 0.0...0.8, value: settings.phosphorPersistence, formatter: { String(format: "%.2f", $0) }),
            Item(key: "vignette", title: "Vignette", range: 0.0...0.8, value: settings.vignetteIntensity, formatter: { String(format: "%.2f", $0) }),
            Item(key: "bloom", title: "Bloom", range: 0.0...0.6, value: settings.bloomIntensity, formatter: { String(format: "%.2f", $0) }),
            Item(key: "noise", title: "Noise", range: 0.0...0.2, value: settings.noiseIntensity, formatter: { String(format: "%.2f", $0) }),
        ]

        layoutSliders()
    }

    private func layoutSliders() {
        let startY = panelHeight/2 - 90
        let gap: CGFloat = 42
        for (index, item) in items.enumerated() {
            let y = startY - CGFloat(index) * gap
            addSlider(for: item, y: y)
        }
    }

    private func addSlider(for item: Item, y: CGFloat) {
        let titleLabel = SKLabelNode(text: item.title)
        titleLabel.fontSize = 16
        titleLabel.horizontalAlignmentMode = .left
        titleLabel.verticalAlignmentMode = .center
        titleLabel.fontColor = .white
        titleLabel.position = CGPoint(x: -panelWidth/2 + margin, y: y)
        addChild(titleLabel)

        let valueLabel = SKLabelNode(text: item.formatter(item.value))
        valueLabel.fontSize = 14
        valueLabel.horizontalAlignmentMode = .right
        valueLabel.verticalAlignmentMode = .center
        valueLabel.fontColor = SKColor(white: 0.9, alpha: 0.85)
        valueLabel.position = CGPoint(x: panelWidth/2 - margin, y: y)
        addChild(valueLabel)
        valueLabels[item.key] = valueLabel

        let track = SKShapeNode(rectOf: CGSize(width: trackWidth, height: trackHeight), cornerRadius: trackHeight/2)
        track.fillColor = SKColor(white: 1.0, alpha: 0.3)
        track.strokeColor = SKColor(white: 1.0, alpha: 0.1)
        track.position = CGPoint(x: -panelWidth/2 + margin + 110 + trackWidth/2, y: y)
        track.name = "track-\(item.key)"
        addChild(track)
        trackNodes[item.key] = track

        let knob = SKShapeNode(circleOfRadius: knobRadius)
        knob.fillColor = SKColor(red: 0.2, green: 0.8, blue: 1.0, alpha: 0.95)
        knob.strokeColor = SKColor(white: 1.0, alpha: 0.6)
        knob.name = "knob-\(item.key)"
        knob.position = positionForValue(item.value, range: item.range, onTrack: track)
        addChild(knob)
        knobNodes[item.key] = knob
    }

    private func positionForValue(_ value: Float, range: ClosedRange<Float>, onTrack track: SKShapeNode) -> CGPoint {
        let t = CGFloat((value - range.lowerBound) / (range.upperBound - range.lowerBound))
        let x0 = track.position.x - trackWidth/2
        let x1 = track.position.x + trackWidth/2
        return CGPoint(x: x0 + (x1 - x0) * t, y: track.position.y)
    }

    private func valueForPosition(_ pos: CGPoint, key: String) -> Float? {
        guard let track = trackNodes[key] else { return nil }
        let x0 = track.position.x - trackWidth/2
        let x1 = track.position.x + trackWidth/2
        let clampedX = max(x0, min(x1, pos.x))
        let t = Float((clampedX - x0) / (x1 - x0))
        guard let item = items.first(where: { $0.key == key }) else { return nil }
        return item.range.lowerBound + (item.range.upperBound - item.range.lowerBound) * t
    }

    private func updateValueLabel(_ key: String, _ value: Float) {
        guard let item = items.first(where: { $0.key == key }), let label = valueLabels[key] else { return }
        label.text = item.formatter(value)
    }

    // MARK: - Interaction
    override func mouseDown(with event: NSEvent) { handle(point: event.location(in: self)) }
    override func mouseDragged(with event: NSEvent) { handle(point: event.location(in: self)) }
    override func mouseUp(with event: NSEvent) { /* no-op */ }

    #if os(iOS)
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) { if let p = touches.first?.location(in: self) { handle(point: p) } }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) { if let p = touches.first?.location(in: self) { handle(point: p) } }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {}
    #endif

    private func handle(point: CGPoint) {
        // Close button
        if atPoint(point).name == "btn-close" { onClose?(); return }
        // Find nearest track/knob to update
        for (key, knob) in knobNodes {
            // If pointer is near this track vertically
            if let track = trackNodes[key], abs(point.y - track.position.y) < 20 {
                let value = valueForPosition(point, key: key) ?? 0
                knob.position = positionForValue(value, range: (items.first{ $0.key == key }!.range), onTrack: track)
                updateValueLabel(key, value)
                onValueChanged?(key, value)
            }
        }
    }

    // External forwarding from Scene (convert scene point to local and handle)
    func receivePointFromScene(_ scenePoint: CGPoint, in scene: SKNode) {
        let p = convert(scenePoint, from: scene)
        handle(point: p)
    }
}
