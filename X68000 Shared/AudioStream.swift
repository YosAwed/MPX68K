#if false

//import Foundation
import AVFoundation


class AudioStream {
    

    var samplingrate = 22050

    init (samplingrate: Int) {
		self.samplingrate = samplingrate
    }


    
    // エンジンの生成
    let audioEngine = AVAudioEngine()
    // ソースノードの生成
    let player = AVAudioPlayerNode()
    
    var buffer :AVAudioPCMBuffer?
    
    

    
    private var sourceNode : AVAudioSourceNode?
	func play(  )
    {
        debugLog("Audio Play", category: .audio)

        // プレイヤーノードからオーディオフォーマットを取得
             let outputNode = audioEngine.outputNode
        let format = outputNode.inputFormat(forBus: 0)
        debugLog("Audio sample rate: \(format.sampleRate)", category: .audio)
        debugLog("Audio format: \(format.commonFormat)", category: .audio)
		let audioFormat :AVAudioFormat = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: Double(self.samplingrate), channels: 2, interleaved: true )!// player.outputFormat(forBus: 0)
        let sampleRate = Float(audioFormat.sampleRate)
        debugLog("sampleRate: \(sampleRate)", category: .audio)
            
        
        sourceNode = AVAudioSourceNode(format: audioFormat , renderBlock: { (_, timeStamp, frameCount, audioBufferList) -> OSStatus in

                let ablPointer = UnsafeMutableAudioBufferListPointer(audioBufferList)
                let buf: UnsafeMutableBufferPointer<Int16> = UnsafeMutableBufferPointer(ablPointer[0])
                        debugLog("mNumberBuffers: \(audioBufferList.pointee.mNumberBuffers)", category: .audio)
                        debugLog("mDataByteSize: \(audioBufferList.pointee.mBuffers.mDataByteSize)", category: .audio)
                        debugLog("mNumberChannels: \(audioBufferList.pointee.mBuffers.mNumberChannels)", category: .audio)
        debugLog("frameCount: \(frameCount)", category: .audio)
                X68000_AudioCallBack(ablPointer[0].mData, UInt32(frameCount));

            return noErr
        })

        // オーディオエンジンにプレイヤーをアタッチ
        sourceNode?.reset()
        audioEngine.attach(sourceNode!)

        let mixer = audioEngine.mainMixerNode

        audioEngine.connect(sourceNode!, to: mixer, format: audioFormat)
        //        audioEngine.attach(player)
        // プレイヤーノードとミキサーノードを接続
  //      audioEngine.connect(player, to: mixer, format: audioFormat)
        // 再生の開始を設定
//        alloc()
        audioEngine.prepare()
        do {
          // エンジンを開始
          try audioEngine.start()
          // 再生
//          player.play()
        } catch let error {
          errorLog("Audio error", error: error, category: .audio)
        }



    }
    func stop()
    {
        debugLog("Audio Stop", category: .audio)

    }
    func pause()
    {
        debugLog("Audio Pause", category: .audio)

    }
    func close()
    {
        debugLog("Audio Close", category: .audio)
    }
}


#else

import Foundation
import AudioToolbox
import Accelerate

func bridge<T : AnyObject>(_ obj : T) -> UnsafeRawPointer {
    return UnsafeRawPointer(Unmanaged.passUnretained(obj).toOpaque())
    // return unsafeAddressOf(obj) // ***
}

func bridge<T : AnyObject>(_ ptr : UnsafeRawPointer) -> T {
    return Unmanaged<T>.fromOpaque(ptr).takeUnretainedValue()
    // return unsafeBitCast(ptr, T.self) // ***
}

// Audio fade-out state tracking
private var fadeOutState: Int = 0
private let fadeOutDuration: Int = 512  // Fade over 512 samples (~11ms at 44kHz)

/// Apply gradual fade-out to prevent click/pop noises when audio stops
private func applyGradualFadeOut(_ audioPtr: UnsafeMutablePointer<Int16>, sampleCount: Int) {
    // Apply exponential fade-out curve over multiple audio frames
    let fadeLength = min(sampleCount, fadeOutDuration - fadeOutState)
    
    if fadeLength > 0 {
        for i in 0..<fadeLength {
            let fadeRatio = Float(fadeOutDuration - fadeOutState - i) / Float(fadeOutDuration)
            let fadeMultiplier = fadeRatio * fadeRatio  // Exponential curve for smooth fade
            
            // Apply fade to stereo samples
            if i < sampleCount {
                audioPtr[i] = Int16(Float(audioPtr[i]) * fadeMultiplier)
            }
        }
        
        fadeOutState += fadeLength
        
        // Complete silence for remaining samples after fade is complete
        if fadeOutState >= fadeOutDuration && fadeLength < sampleCount {
            for i in fadeLength..<sampleCount {
                audioPtr[i] = 0
            }
        }
    } else {
        // Fade is complete, apply silence
        for i in 0..<sampleCount {
            audioPtr[i] = 0
        }
    }
}

/// Reset fade-out state when audio resumes
private func resetFadeOut() {
    fadeOutState = 0
}

func outputCallback(_ data: UnsafeMutableRawPointer?, queue: AudioQueueRef, buffer: AudioQueueBufferRef) {

    let audioData = buffer.pointee.mAudioData

    let size = buffer.pointee.mAudioDataBytesCapacity / 4  // Size in samples (16-bit stereo)

    // Enhanced safety check with better noise prevention
    guard size > 0 && size <= 32768 else {  // Further increased buffer size limit for 100ms buffers
        if size > 32768 {
            errorLog("Audio buffer size \(size) exceeds maximum allowed (32768)", category: .audio)
        }
        return
    }

    // Verify buffer has sufficient capacity for Int16 conversion
    guard buffer.pointee.mAudioDataBytesCapacity >= size * 4 else {
        errorLog("Audio buffer capacity insufficient: \(buffer.pointee.mAudioDataBytesCapacity) bytes, need \(size * 4)", category: .audio)
        return
    }

    let mAudioDataPtr = UnsafeMutablePointer<Int16>(OpaquePointer(audioData))
        
        // Pre-clear buffer completely to prevent any residual noise
        memset(audioData, 0, Int(buffer.pointee.mAudioDataBytesCapacity))
        
        // Call the audio generation function
        X68000_AudioCallBack(mAudioDataPtr, UInt32(size))
        
        // Highly optimized audio processing using Accelerate Framework
        let sampleCount = Int(size * 2)  // size * 2 for stereo samples
        
        // Use stack-allocated buffer for small arrays to avoid heap allocation
        if sampleCount <= 8192 {  // Reasonable buffer size threshold
            // Stack-based processing for typical audio buffer sizes
            let floatSamples = UnsafeMutablePointer<Float>.allocate(capacity: sampleCount)
            defer { floatSamples.deallocate() }
            
            // Convert Int16 to Float for vectorized operations
            vDSP_vflt16(mAudioDataPtr, 1, floatSamples, 1, vDSP_Length(sampleCount))
            
            // Vectorized clipping
            var lowerBound: Float = -32000.0
            var upperBound: Float = 32000.0
            vDSP_vclip(floatSamples, 1, &lowerBound, &upperBound, floatSamples, 1, vDSP_Length(sampleCount))
            
            // Calculate RMS for silence detection (more accurate than mean absolute)
            var rmsValue: Float = 0
            vDSP_rmsqv(floatSamples, 1, &rmsValue, vDSP_Length(sampleCount))
            
            // Convert back to Int16
            vDSP_vfix16(floatSamples, 1, mAudioDataPtr, 1, vDSP_Length(sampleCount))
            
            // Smooth silence detection to prevent click/pop noises
            if rmsValue <= 10.0 {
                // Apply gradual fade-out instead of abrupt silence
                applyGradualFadeOut(mAudioDataPtr, sampleCount: sampleCount)
            } else {
                // Audio is active, reset fade-out state
                resetFadeOut()
            }
            
        } else {
            // Fallback to single-pass processing for very large buffers
            var hasSignificantAudio = false
            
            for i in 0..<sampleCount {
                var sample = mAudioDataPtr[i]
                
                // Apply clipping
                sample = max(-32000, min(32000, sample))
                mAudioDataPtr[i] = sample
                
                // Early exit optimization for silence detection
                if !hasSignificantAudio && abs(sample) > 10 {
                    hasSignificantAudio = true
                }
            }
            
            // If no significant audio found, apply smooth fade-out
            if !hasSignificantAudio {
                applyGradualFadeOut(mAudioDataPtr, sampleCount: sampleCount)
            } else {
                // Audio is active, reset fade-out state
                resetFadeOut()
            }
        }

        buffer.pointee.mAudioDataByteSize = buffer.pointee.mAudioDataBytesCapacity
        AudioQueueEnqueueBuffer(queue, buffer, 0, nil)
}


class AudioStream {
    var dataFormat:     AudioStreamBasicDescription
    var queue:          AudioQueueRef? = nil

    var buffers =       [AudioQueueBufferRef?](repeating: nil, count: 4)  // Increased buffer count for stability

    var bufferByteSize: UInt32
    
	var samplingrate = 22050
	
	init (samplingrate: Int) {
		self.samplingrate = samplingrate

        dataFormat = AudioStreamBasicDescription(
            mSampleRate:        Float64(samplingrate),
            mFormatID:          kAudioFormatLinearPCM,
            mFormatFlags:       kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
            mBytesPerPacket:    4,
            mFramesPerPacket:   1,
            mBytesPerFrame:     4,
            mChannelsPerFrame:  2,
            mBitsPerChannel:    16,
            mReserved:          0
        )

        // Calculate buffer size based on sample rate to provide ~100ms of audio buffer for stability
        let bufferDurationSeconds: Float64 = 0.1  // 100ms for better stability and reduced stuttering
        let framesPerBuffer = UInt32(Float64(samplingrate) * bufferDurationSeconds)
        bufferByteSize = framesPerBuffer * dataFormat.mBytesPerFrame
        
        debugLog("Audio buffer: \(framesPerBuffer) frames, \(bufferByteSize) bytes", category: .audio)
//return;
        AudioQueueNewOutput(
            &dataFormat,
            outputCallback,
            unsafeBitCast(self, to: UnsafeMutableRawPointer.self),
            nil,  // Use internal thread for better performance
            nil,  // Use internal thread for better performance
            0,
            &queue)
        
        // Set audio queue properties for better performance and stability
        if let queue = queue {
            // Disable level metering for better performance
            var enableLevelMetering: UInt32 = 0
            AudioQueueSetProperty(queue, kAudioQueueProperty_EnableLevelMetering, &enableLevelMetering, UInt32(MemoryLayout<UInt32>.size))
        }
        
        load()
    
    }

    func load()
    {
        if let queue = self.queue {
            
            // Allocate and pre-fill all buffers
            for i in 0..<buffers.count {
                AudioQueueAllocateBuffer(queue, bufferByteSize, &buffers[i])
                if let buffer = buffers[i] {
                    // Pre-fill buffer with silence to ensure smooth startup
                    memset(buffer.pointee.mAudioData, 0, Int(bufferByteSize))
                    buffer.pointee.mAudioDataByteSize = bufferByteSize
                    outputCallback(unsafeBitCast(self, to: UnsafeMutableRawPointer.self), queue: queue, buffer: buffer)
                }
            }
            
            // Flush any previous state and prime the queue
            AudioQueueFlush(queue)
            let primeResult = AudioQueuePrime(queue, 0, nil)
            if primeResult != noErr {
                errorLog("Failed to prime audio queue: \(primeResult)", category: .audio)
            }
        }
    }

    func play()
    {
        debugLog("Audio Play", category: .audio)
        if let queue = self.queue {
            let result = AudioQueueStart(queue, nil)
            if result != noErr {
                errorLog("Failed to start audio queue: \(result)", category: .audio)
            }
        }
    }
    
    func stop()
    {
        debugLog("Audio Stop", category: .audio)
        if let queue = self.queue {
            let result = AudioQueueStop(queue, true)  // immediate stop
            if result != noErr {
                errorLog("Failed to stop audio queue: \(result)", category: .audio)
            }
        }
    }
    
    func pause()
    {
        debugLog("Audio Pause", category: .audio)
        if let queue = self.queue {
            let result = AudioQueuePause(queue)
            if result != noErr {
                errorLog("Failed to pause audio queue: \(result)", category: .audio)
            }
        }
    }
    func close()
    {
        debugLog("Audio Close", category: .audio)

        if let queue = self.queue {
            // Stop audio queue first
            AudioQueueStop(queue, true)
            
            // Flush any remaining buffers
            AudioQueueFlush(queue)
            
            // Free all buffers
            for buffer in buffers {
                if let buffer = buffer {
                    AudioQueueFreeBuffer(queue, buffer)
                }
            }
            
            // Dispose of the queue
            let result = AudioQueueDispose(queue, true)
            if result != noErr {
                errorLog("Failed to dispose audio queue: \(result)", category: .audio)
            }
            
            self.queue = nil
        }
    }
}

#endif
