import CoreMotion
import simd

/// Phone gyro -> head orientation in OpenVR space (y up, -z forward).
/// Assumes the phone is held landscape with its top edge toward your LEFT (like a Cardboard viewer).
/// If looking left/right or up/down feels mirrored, the Mdh matrix below is the one place to change.
final class Head: ObservableObject {
    private let mm = CMMotionManager()
    private(set) var q = simd_quatf(ix: 0, iy: 0, iz: 0, r: 1)
    private var raw = matrix_identity_float3x3
    private var yawOffset = simd_quatf(ix: 0, iy: 0, iz: 0, r: 1)
    private var needsCenter = true

    private let rw = simd_float3x3(columns: (SIMD3(1, 0, 0), SIMD3(0, 0, -1), SIMD3(0, 1, 0)))   // Apple Z-up -> OpenVR Y-up
    private let mdh = simd_float3x3(columns: (SIMD3(0, 1, 0), SIMD3(-1, 0, 0), SIMD3(0, 0, 1)))  // device axes -> head axes

    func start() {
        mm.deviceMotionUpdateInterval = 1.0 / 90
        mm.startDeviceMotionUpdates(using: .xArbitraryZVertical, to: .main) { [weak self] dm, _ in
            guard let self = self, let a = dm?.attitude.quaternion else { return }
            let att = simd_float3x3(simd_quatf(ix: Float(a.x), iy: Float(a.y), iz: Float(a.z), r: Float(a.w)))
            self.raw = self.rw * att * self.mdh.transpose
            if self.needsCenter { self.recenter(); self.needsCenter = false }
            self.q = self.yawOffset * simd_quatf(self.raw)
        }
    }

    func recenter() {
        let f = raw * SIMD3<Float>(0, 0, -1)
        let yaw = atan2(-f.x, -f.z)
        yawOffset = simd_quatf(angle: -yaw, axis: SIMD3(0, 1, 0))
    }
}
