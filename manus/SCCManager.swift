// SCCManager.swift integrated from provided sample (minor adjustments)
import Foundation

public enum SCCMode: Int32 {
    case mouseOnly = 0
    case serialPTY = 1
    case serialTCP = 2
    case serialFile = 3
}

public class SCCManager: ObservableObject {
    @Published public var isConnected: Bool = false
    @Published public var currentMode: SCCMode = .mouseOnly
    @Published public var slavePath: String = ""
    @Published public var connectionInfo: String = ""
    @Published public var baudRate: Int = 9600

    private var isInitialized: Bool = false

    public init() { initializeSCC() }
    deinit { disconnect() }

    private func initializeSCC() {
        if !isInitialized {
            SCC_Init()
            isInitialized = true
        }
    }

    public func createPTY() -> Bool {
        let result = SCC_SetMode(SCCMode.serialPTY.rawValue, nil)
        if result == 0 {
            currentMode = .serialPTY
            isConnected = true
            updateConnectionInfo()
            return true
        }
        return false
    }

    public func connectTCP(host: String, port: Int) -> Bool {
        let config = "\(host):\(port)"
        let result = config.withCString { ptr in SCC_SetMode(SCCMode.serialTCP.rawValue, ptr) }
        if result == 0 {
            currentMode = .serialTCP
            isConnected = true
            connectionInfo = "TCP: \(host):\(port)"
            return true
        }
        return false
    }

    public func connectSerial(devicePath: String) -> Bool {
        let result = devicePath.withCString { ptr in SCC_SetMode(SCCMode.serialFile.rawValue, ptr) }
        if result == 0 {
            currentMode = .serialFile
            isConnected = true
            connectionInfo = "Serial: \(devicePath)"
            return true
        }
        return false
    }

    public func setMouseOnlyMode() -> Bool {
        let result = SCC_SetMode(SCCMode.mouseOnly.rawValue, nil)
        if result == 0 {
            currentMode = .mouseOnly
            isConnected = false
            connectionInfo = "Mouse Only"
            slavePath = ""
            return true
        }
        return false
    }

    public func disconnect() {
        SCC_CloseSerial()
        isConnected = false
        connectionInfo = ""
        slavePath = ""
    }

    private func updateConnectionInfo() {
        switch currentMode {
        case .mouseOnly: connectionInfo = "Mouse Only"
        case .serialPTY: 
            if let path = getPTYSlavePath() {
                slavePath = path
                connectionInfo = "PTY: \(path)"
            } else {
                connectionInfo = "PTY"
            }
        case .serialTCP: connectionInfo = "TCP Connection"
        case .serialFile: connectionInfo = "Serial Device"
        }
    }
    
    public func getPTYSlavePath() -> String? {
        if let cPath = SCC_GetSlavePath() {
            return String(cString: cPath)
        }
        return nil
    }
    
    public func getScreenCommand() -> String? {
        if let slavePath = getPTYSlavePath() {
            return "screen \(slavePath) \(baudRate)"
        }
        return nil
    }
    
    public func setBaudRate(_ rate: Int) {
        baudRate = rate
    }
    
    public static let availableBaudRates: [Int] = [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200]

    public func getAvailableSerialDevices() -> [String] {
        var devices: [String] = []
        do {
            let contents = try FileManager.default.contentsOfDirectory(atPath: "/dev")
            for item in contents { if item.hasPrefix("tty.") || item.hasPrefix("cu.") { devices.append("/dev/\(item)") } }
        } catch { }
        return devices.sorted()
    }
}

@_silgen_name("SCC_Init") func SCC_Init()
@_silgen_name("SCC_SetMode") func SCC_SetMode(_ mode: Int32, _ config: UnsafePointer<CChar>?) -> Int32
@_silgen_name("SCC_CloseSerial") func SCC_CloseSerial()
@_silgen_name("SCC_GetSlavePath") func SCC_GetSlavePath() -> UnsafePointer<CChar>?
