// Madgwick AHRS (S. Madgwick, 2010) - straight port of the reference C code.
//
// Frames:  body   = sensor frame (x,y,z as streamed by the board)
//          earth  = NWU: x = magnetic north, y = west, z = up
// q = [w, x, y, z] rotates body-frame vectors into the earth frame, which is
// exactly what three.js Object3D.quaternion expects.
//
// Inputs: gyro in rad/s, accel in any unit (normalised), mag in any unit (normalised).

export class Madgwick {
  constructor(beta = 0.08) {
    this.beta = beta;
    this.q = [1, 0, 0, 0];
  }

  reset() {
    this.q = [1, 0, 0, 0];
  }

  /** 9-DOF update. Falls back to 6-DOF if the magnetometer reading is all zero. */
  update(gx, gy, gz, ax, ay, az, mx, my, mz, dt) {
    if (mx === 0 && my === 0 && mz === 0) return this.updateIMU(gx, gy, gz, ax, ay, az, dt);

    let [q0, q1, q2, q3] = this.q;

    // Rate of change of quaternion from gyroscope
    let qDot1 = 0.5 * (-q1 * gx - q2 * gy - q3 * gz);
    let qDot2 = 0.5 * (q0 * gx + q2 * gz - q3 * gy);
    let qDot3 = 0.5 * (q0 * gy - q1 * gz + q3 * gx);
    let qDot4 = 0.5 * (q0 * gz + q1 * gy - q2 * gx);

    if (!(ax === 0 && ay === 0 && az === 0)) {
      let n = 1 / Math.hypot(ax, ay, az);
      ax *= n; ay *= n; az *= n;
      n = 1 / Math.hypot(mx, my, mz);
      mx *= n; my *= n; mz *= n;

      const _2q0mx = 2 * q0 * mx, _2q0my = 2 * q0 * my, _2q0mz = 2 * q0 * mz, _2q1mx = 2 * q1 * mx;
      const _2q0 = 2 * q0, _2q1 = 2 * q1, _2q2 = 2 * q2, _2q3 = 2 * q3, _2q0q2 = 2 * q0 * q2, _2q2q3 = 2 * q2 * q3;
      const q0q0 = q0 * q0, q0q1 = q0 * q1, q0q2 = q0 * q2, q0q3 = q0 * q3;
      const q1q1 = q1 * q1, q1q2 = q1 * q2, q1q3 = q1 * q3, q2q2 = q2 * q2, q2q3 = q2 * q3, q3q3 = q3 * q3;

      // Reference direction of Earth's magnetic field
      const hx = mx * q0q0 - _2q0my * q3 + _2q0mz * q2 + mx * q1q1 + _2q1 * my * q2 + _2q1 * mz * q3 - mx * q2q2 - mx * q3q3;
      const hy = _2q0mx * q3 + my * q0q0 - _2q0mz * q1 + _2q1mx * q2 - my * q1q1 + my * q2q2 + _2q2 * mz * q3 - my * q3q3;
      const _2bx = Math.sqrt(hx * hx + hy * hy);
      const _2bz = -_2q0mx * q2 + _2q0my * q1 + mz * q0q0 + _2q1mx * q3 - mz * q1q1 + _2q2 * my * q3 - mz * q2q2 + mz * q3q3;
      const _4bx = 2 * _2bx, _4bz = 2 * _2bz;

      // Gradient descent corrective step
      const fa1 = 2 * q1q3 - _2q0q2 - ax;
      const fa2 = 2 * q0q1 + _2q2q3 - ay;
      const fa3 = 1 - 2 * q1q1 - 2 * q2q2 - az;
      const fm1 = _2bx * (0.5 - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx;
      const fm2 = _2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my;
      const fm3 = _2bx * (q0q2 + q1q3) + _2bz * (0.5 - q1q1 - q2q2) - mz;

      const s0 = -_2q2 * fa1 + _2q1 * fa2 - _2bz * q2 * fm1 + (-_2bx * q3 + _2bz * q1) * fm2 + _2bx * q2 * fm3;
      const s1 = _2q3 * fa1 + _2q0 * fa2 - 4 * q1 * fa3 + _2bz * q3 * fm1 + (_2bx * q2 + _2bz * q0) * fm2 + (_2bx * q3 - _4bz * q1) * fm3;
      const s2 = -_2q0 * fa1 + _2q3 * fa2 - 4 * q2 * fa3 + (-_4bx * q2 - _2bz * q0) * fm1 + (_2bx * q1 + _2bz * q3) * fm2 + (_2bx * q0 - _4bz * q2) * fm3;
      const s3 = _2q1 * fa1 + _2q2 * fa2 + (-_4bx * q3 + _2bz * q1) * fm1 + (-_2bx * q0 + _2bz * q2) * fm2 + _2bx * q1 * fm3;

      n = Math.hypot(s0, s1, s2, s3);
      if (n > 0) {   // zero gradient -> no correction (avoids 0 * Infinity = NaN)
        qDot1 -= this.beta * s0 / n;
        qDot2 -= this.beta * s1 / n;
        qDot3 -= this.beta * s2 / n;
        qDot4 -= this.beta * s3 / n;
      }
    }

    this._integrate(q0, q1, q2, q3, qDot1, qDot2, qDot3, qDot4, dt);
  }

  /** 6-DOF update (gyro + accel only). Yaw will drift slowly. */
  updateIMU(gx, gy, gz, ax, ay, az, dt) {
    let [q0, q1, q2, q3] = this.q;

    let qDot1 = 0.5 * (-q1 * gx - q2 * gy - q3 * gz);
    let qDot2 = 0.5 * (q0 * gx + q2 * gz - q3 * gy);
    let qDot3 = 0.5 * (q0 * gy - q1 * gz + q3 * gx);
    let qDot4 = 0.5 * (q0 * gz + q1 * gy - q2 * gx);

    if (!(ax === 0 && ay === 0 && az === 0)) {
      let n = 1 / Math.hypot(ax, ay, az);
      ax *= n; ay *= n; az *= n;

      const _2q0 = 2 * q0, _2q1 = 2 * q1, _2q2 = 2 * q2, _2q3 = 2 * q3;
      const _4q0 = 4 * q0, _4q1 = 4 * q1, _4q2 = 4 * q2, _8q1 = 8 * q1, _8q2 = 8 * q2;
      const q0q0 = q0 * q0, q1q1 = q1 * q1, q2q2 = q2 * q2, q3q3 = q3 * q3;

      const s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
      const s1 = _4q1 * q3q3 - _2q3 * ax + 4 * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
      const s2 = 4 * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
      const s3 = 4 * q1q1 * q3 - _2q1 * ax + 4 * q2q2 * q3 - _2q2 * ay;

      n = Math.hypot(s0, s1, s2, s3);
      if (n > 0) {
        qDot1 -= this.beta * s0 / n;
        qDot2 -= this.beta * s1 / n;
        qDot3 -= this.beta * s2 / n;
        qDot4 -= this.beta * s3 / n;
      }
    }

    this._integrate(q0, q1, q2, q3, qDot1, qDot2, qDot3, qDot4, dt);
  }

  _integrate(q0, q1, q2, q3, d1, d2, d3, d4, dt) {
    q0 += d1 * dt; q1 += d2 * dt; q2 += d3 * dt; q3 += d4 * dt;
    const n = 1 / Math.hypot(q0, q1, q2, q3);
    this.q = [q0 * n, q1 * n, q2 * n, q3 * n];
  }

  /** Euler angles in degrees: roll (about x), pitch (about y), yaw (about z, CCW from north). */
  euler() {
    const [q0, q1, q2, q3] = this.q;
    const roll = Math.atan2(2 * (q0 * q1 + q2 * q3), 1 - 2 * (q1 * q1 + q2 * q2));
    const pitch = Math.asin(Math.max(-1, Math.min(1, 2 * (q0 * q2 - q3 * q1))));
    const yaw = Math.atan2(2 * (q0 * q3 + q1 * q2), 1 - 2 * (q2 * q2 + q3 * q3));
    const d = 180 / Math.PI;
    return { roll: roll * d, pitch: pitch * d, yaw: yaw * d };
  }

  /**
   * Compass heading in degrees, clockwise from magnetic north (0..360), of the
   * body +Y axis (the Grove-port edge) - same convention as a phone's azimuth.
   */
  heading() {
    const [q0, q1, q2, q3] = this.q;
    const north = 2 * (q1 * q2 - q0 * q3);      // earth-x component of body +Y
    const west = 1 - 2 * (q1 * q1 + q3 * q3);   // earth-y component of body +Y
    return (Math.atan2(-west, north) * 180 / Math.PI + 360) % 360;
  }
}
