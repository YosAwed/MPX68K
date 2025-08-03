# MPX68K Software Architecture

This document describes the software architecture of MPX68K, a Sharp X68000 emulator for iOS and macOS platforms.

## Overall System Architecture

```mermaid
graph TB
    subgraph "User Interface Layer"
        iOS[iOS App]
        macOS[macOS App]
    end
    
    subgraph "Swift Shared Layer"
        GameScene[GameScene.swift<br/>SpriteKit Viewport]
        FileSystem[FileSystem.swift<br/>Document Management]
        AudioStream[AudioStream.swift<br/>Audio Bridge]
        JoyCard[X68JoyCard.swift<br/>Input Controller]
    end
    
    subgraph "C/C++ Emulation Core"
        PX68K[px68k Core<br/>X68000 Hardware]
        M68K[M68000 CPU Wrapper]
        FMGen[fmgen<br/>FM Sound Engine]
        X68KHW[X68000 Hardware<br/>FDC, SCSI, ADPCF, Graphics]
    end
    
    subgraph "CPU Emulator"
        C68K[C68K Library<br/>M68000 CPU Core]
    end
    
    subgraph "Apple Frameworks"
        SpriteKit[SpriteKit]
        AVFoundation[AVFoundation]
        GameplayKit[GameplayKit]
        CloudKit[CloudKit/iCloud]
        UTI[UniformTypeIdentifiers]
    end
    
    iOS --> GameScene
    macOS --> GameScene
    
    GameScene --> SpriteKit
    GameScene --> JoyCard
    GameScene --> PX68K
    
    FileSystem --> CloudKit
    FileSystem --> UTI
    
    AudioStream --> AVFoundation
    AudioStream --> FMGen
    
    JoyCard --> GameplayKit
    JoyCard --> PX68K
    
    PX68K --> M68K
    PX68K --> FMGen
    PX68K --> X68KHW
    
    M68K --> C68K
    
    style iOS fill:#e1f5fe
    style macOS fill:#e8f5e8
    style GameScene fill:#fff3e0
    style PX68K fill:#fce4ec
    style C68K fill:#f3e5f5
```

## Platform-Specific Architecture

```mermaid
graph LR
    subgraph "iOS Platform"
        iOSUI[iOS UI Layer]
        TouchInput[Touch Input]
        DocBrowser[Document Browser]
        iOSLife[App Lifecycle]
    end
    
    subgraph "macOS Platform"
        macOSUI[macOS UI Layer]
        MenuBar[Menu Bar Integration]
        KeyMouse[Keyboard/Mouse Input]
        DragDrop[Drag & Drop]
        WindowMgmt[Window Management]
    end
    
    subgraph "Shared Core"
        SharedCore[X68000 Shared<br/>Business Logic]
    end
    
    iOSUI --> SharedCore
    TouchInput --> SharedCore
    DocBrowser --> SharedCore
    iOSLife --> SharedCore
    
    macOSUI --> SharedCore
    MenuBar --> SharedCore
    KeyMouse --> SharedCore
    DragDrop --> SharedCore
    WindowMgmt --> SharedCore
    
    style iOSUI fill:#e1f5fe
    style macOSUI fill:#e8f5e8
    style SharedCore fill:#fff3e0
```

## Data Flow Architecture

```mermaid
sequenceDiagram
    participant User
    participant Swift_UI
    participant GameScene
    participant PX68K_Core
    participant C68K_CPU
    participant Audio_System
    
    User->>Swift_UI: Input (Touch/Keyboard/Mouse)
    Swift_UI->>GameScene: Process Input
    GameScene->>PX68K_Core: Emulation Step
    PX68K_Core->>C68K_CPU: Execute CPU Instructions
    C68K_CPU-->>PX68K_Core: CPU State Update
    PX68K_Core->>Audio_System: Generate Audio
    Audio_System-->>Swift_UI: Audio Output
    PX68K_Core-->>GameScene: Screen Update
    GameScene-->>Swift_UI: Render Frame
    Swift_UI-->>User: Display & Audio
```

## File System Architecture

```mermaid
graph TD
    subgraph "File Sources"
        iCloudDocs[iCloud Documents]
        LocalDocs[Local Documents]
        AppBundle[App Bundle ROMs]
        Inbox[App Inbox]
    end
    
    subgraph "File Management"
        FileSystem[FileSystem.swift]
        SecurityScope[Security Scoped Resources]
        UTIHandler[UTI Handler]
    end
    
    subgraph "File Types"
        ROM[ROM Files<br/>CGROM.DAT, IPLROM.DAT]
        Floppy[Floppy Images<br/>.dim, .xdf, .d88]
        HDD[Hard Disk Images<br/>.hdf, .hdm]
        SaveData[Save Data<br/>SRAM.DAT]
    end
    
    subgraph "Emulation Core"
        PX68K_FS[px68k File Access]
    end
    
    iCloudDocs --> FileSystem
    LocalDocs --> FileSystem
    AppBundle --> FileSystem
    Inbox --> FileSystem
    
    FileSystem --> SecurityScope
    FileSystem --> UTIHandler
    
    FileSystem --> ROM
    FileSystem --> Floppy
    FileSystem --> HDD
    FileSystem --> SaveData
    
    ROM --> PX68K_FS
    Floppy --> PX68K_FS
    HDD --> PX68K_FS
    SaveData --> PX68K_FS
    
    style iCloudDocs fill:#e3f2fd
    style FileSystem fill:#fff3e0
    style PX68K_FS fill:#fce4ec
```

## Audio System Architecture

```mermaid
graph TB
    subgraph "C++ Audio Generation"
        FMGen[fmgen Engine<br/>FM Synthesis]
        ADPCM[ADPCM Audio]
        PCM[PCM Audio]
    end
    
    subgraph "Swift Audio Bridge"
        AudioStream[AudioStream.swift]
        AudioBuffer[Audio Buffer Management]
    end
    
    subgraph "Apple Audio Framework"
        AVAudio[AVFoundation]
        AudioOutput[Audio Output]
    end
    
    subgraph "X68000 Hardware"
        OPM[OPM Chip Emulation]
        AudioHW[Audio Hardware]
    end
    
    OPM --> FMGen
    AudioHW --> ADPCM
    AudioHW --> PCM
    
    FMGen --> AudioStream
    ADPCM --> AudioStream
    PCM --> AudioStream
    
    AudioStream --> AudioBuffer
    AudioBuffer --> AVAudio
    AVAudio --> AudioOutput
    
    style FMGen fill:#fce4ec
    style AudioStream fill:#fff3e0
    style AVAudio fill:#e8f5e8
```

## Input System Architecture

```mermaid
graph TB
    subgraph "Input Sources"
        Touch[Touch Input<br/>iOS]
        Keyboard[Keyboard Input<br/>macOS]
        Mouse[Mouse Input<br/>macOS]
        GameController[Game Controller<br/>Both Platforms]
    end
    
    subgraph "Swift Input Processing"
        JoyCard[X68JoyCard.swift]
        InputMapper[Input Mapping]
        GameplayKit[GameplayKit Integration]
    end
    
    subgraph "Emulation Core"
        X68KInput[X68000 Input System]
        JoyStick[Joystick Emulation]
        KeyboardEmu[Keyboard Emulation]
        MouseEmu[Mouse Emulation]
    end
    
    Touch --> JoyCard
    Keyboard --> JoyCard
    Mouse --> JoyCard
    GameController --> GameplayKit
    GameplayKit --> JoyCard
    
    JoyCard --> InputMapper
    InputMapper --> X68KInput
    
    X68KInput --> JoyStick
    X68KInput --> KeyboardEmu
    X68KInput --> MouseEmu
    
    style Touch fill:#e1f5fe
    style Keyboard fill:#e8f5e8
    style Mouse fill:#e8f5e8
    style JoyCard fill:#fff3e0
    style X68KInput fill:#fce4ec
```

## Build System Architecture

```mermaid
graph TD
    subgraph "Source Code"
        SwiftCode[Swift Source Code]
        CCode[C Source Code]
        CPPCode[C++ Source Code]
        C68KSource[C68K Source Code]
    end
    
    subgraph "Build Process"
        C68KBuild[C68K Static Library Build]
        MainBuild[Main Project Build]
        BridgingHeaders[Bridging Headers]
    end
    
    subgraph "Output"
        C68KLib[libc68k.a]
        iOSApp[iOS App Bundle]
        macOSApp[macOS App Bundle]
    end
    
    C68KSource --> C68KBuild
    C68KBuild --> C68KLib
    
    SwiftCode --> MainBuild
    CCode --> MainBuild
    CPPCode --> MainBuild
    C68KLib --> MainBuild
    BridgingHeaders --> MainBuild
    
    MainBuild --> iOSApp
    MainBuild --> macOSApp
    
    style C68KBuild fill:#f3e5f5
    style MainBuild fill:#fff3e0
    style C68KLib fill:#e1f5fe
```

## Key Design Patterns

### 1. Multi-Platform Strategy
- **Shared Core**: Common business logic and emulation engine
- **Platform-Specific UI**: Separate iOS and macOS presentation layers
- **Conditional Compilation**: Platform-specific code using `#if os()` directives

### 2. Document-Based Architecture
- **File Type Integration**: Custom UTI declarations for X68000 file formats
- **iCloud Synchronization**: Automatic document sync across devices
- **Sandboxed Access**: Security-scoped resource management

### 3. Bridge Pattern
- **Swift-C Interop**: Bridging headers for C API access from Swift
- **Audio Bridge**: AudioStream class bridges C++ audio to AVFoundation
- **Input Bridge**: Unified input system across different input methods

### 4. Emulation Core Isolation
- **Static Library**: C68K CPU emulator as independent static library
- **C/C++ Core**: px68k emulation engine in separate language layer
- **Minimal Dependencies**: Clean separation between emulation and UI layers

This architecture enables MPX68K to provide authentic X68000 emulation while maintaining modern iOS and macOS user experience standards.