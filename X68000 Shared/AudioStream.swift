
import Foundation
import AudioToolbox

func bridge<T : AnyObject>(_ obj : T) -> UnsafeRawPointer {
    return UnsafeRawPointer(Unmanaged.passUnretained(obj).toOpaque())
    // return unsafeAddressOf(obj) // ***
}

func bridge<T : AnyObject>(_ ptr : UnsafeRawPointer) -> T {
    return Unmanaged<T>.fromOpaque(ptr).takeUnretainedValue()
    // return unsafeBitCast(ptr, T.self) // ***
}

func outputCallback(_ data: UnsafeMutableRawPointer?, queue: AudioQueueRef, buffer: AudioQueueBufferRef) {

//    let stream: AudioStream = bridge(UnsafeRawPointer(data)!)
    
    let size = buffer.pointee.mAudioDataBytesCapacity / 4
    let opaquePtr = OpaquePointer(buffer.pointee.mAudioData)
    let mAudioDataPrt = UnsafeMutablePointer<Int16>(opaquePtr)

    X68000_AudioCallBack(mAudioDataPrt, UInt32(size));


    buffer.pointee.mAudioDataByteSize = buffer.pointee.mAudioDataBytesCapacity
    AudioQueueEnqueueBuffer(queue, buffer, 0, nil)

}


class AudioStream {
    var dataFormat:     AudioStreamBasicDescription
    var queue:          AudioQueueRef?

    var buffers =       [AudioQueueBufferRef?](repeating: nil, count: 2)

    var bufferByteSize: UInt32
    var packetsToPlay: UInt32
    
    init () {

        dataFormat = AudioStreamBasicDescription(
            mSampleRate:        44100/2,
            mFormatID:          kAudioFormatLinearPCM,
            mFormatFlags:       kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked, // Bigendian??
            mBytesPerPacket:    4,
            mFramesPerPacket:   1,
            mBytesPerFrame:     4,
            mChannelsPerFrame:  2,
            mBitsPerChannel:    16,
            mReserved:          0
        )

        bufferByteSize   = 512 * dataFormat.mBytesPerFrame
        packetsToPlay    = 1

        AudioQueueNewOutput(
            &dataFormat,
            outputCallback,
            unsafeBitCast(self, to: UnsafeMutableRawPointer.self),
            CFRunLoopGetCurrent(),
            CFRunLoopMode.commonModes.rawValue,
            0,
            &queue)
        
        load()
    
    }

    func load()
    {
        if let queue = self.queue {
            
            for case var buffer in buffers {
                AudioQueueAllocateBuffer(queue, bufferByteSize, &buffer)
                outputCallback(unsafeBitCast(self, to: UnsafeMutableRawPointer.self), queue: queue, buffer: buffer!)
            }
            AudioQueueFlush(queue)
            AudioQueuePrime(queue,0,nil)
        }
    }

    func play()
    {
        print("Play")
        if let queue = self.queue {
            AudioQueueStart(queue, nil)
        }
    }
    func stop()
    {
        print("Stop")
        if let queue = self.queue {
            AudioQueueStop(queue, true)
        }

    }
    func pause()
    {
        print("Pause")
        if let queue = self.queue {
            AudioQueuePause(queue)
        }

    }
    func close()
    {
        print("Close")

        if let queue = self.queue {
            AudioQueueFlush(queue)
            if let buffer = buffers[0] { AudioQueueFreeBuffer(queue, buffer) } // <- V not necessary/
            if let buffer = buffers[1] { AudioQueueFreeBuffer(queue, buffer) }
            AudioQueueDispose(queue, true)
            self.queue = nil
        }
        

    }
}

