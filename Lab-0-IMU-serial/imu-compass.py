import serial
import re
import math
import time
import pygame

# ============================================================
# Configuration
# ============================================================

SERIAL_PORT = "/dev/cu.usbmodem31101"   # Mac: to check the serial port, run `ls /dev/cu.*` in terminal
# Windows example: "COM5"

BAUD_RATE = 115200

WIDTH = 700
HEIGHT = 700

# ============================================================
# Serial
# ============================================================

ser = serial.Serial(
    SERIAL_PORT,
    BAUD_RATE,
    timeout=0.1
)

# Matches:
# gx:0.123 gy:-0.456 gz:12.345
gyro_pattern = re.compile(
    r"gx:([-\d.]+)\s+gy:([-\d.]+)\s+gz:([-\d.]+)"
)

# ============================================================
# Pygame
# ============================================================

pygame.init()

screen = pygame.display.set_mode((WIDTH, HEIGHT))
pygame.display.set_caption("M5AtomS3 IMU Rotation")

font = pygame.font.SysFont(None, 30)

clock = pygame.time.Clock()

center = (WIDTH // 2, HEIGHT // 2)
radius = 220

# ============================================================
# Orientation
# ============================================================

# Current pointer angle in degrees
angle = 0.0

last_time = time.time()

# Gyroscope dead zone
GYRO_THRESHOLD = 0.5


# ============================================================
# Main loop
# ============================================================

running = True

while running:

    # --------------------------------------------------------
    # Handle GUI events
    # --------------------------------------------------------

    for event in pygame.event.get():
        if event.type == pygame.QUIT:
            running = False

        # Press R to reset angle
        if event.type == pygame.KEYDOWN:
            if event.key == pygame.K_r:
                angle = 0.0

    # --------------------------------------------------------
    # Read serial data
    # --------------------------------------------------------

    while ser.in_waiting:

        try:
            line = ser.readline().decode("utf-8").strip()

            match = gyro_pattern.search(line)

            if match:

                gx = float(match.group(1))
                gy = float(match.group(2))
                gz = float(match.group(3))

                current_time = time.time()

                dt = current_time - last_time
                last_time = current_time

                # Ignore tiny gyro noise
                if abs(gz) < GYRO_THRESHOLD:
                    gz = 0

                # gyro unit: degree / second
                # angle = integral of angular velocity
                angle += gz * dt

                # Keep angle between 0 ~ 360
                angle %= 360

        except Exception as e:
            print("Serial error:", e)

    # --------------------------------------------------------
    # Draw
    # --------------------------------------------------------

    screen.fill((245, 245, 245))

    # Ball
    pygame.draw.circle(
        screen,
        (220, 230, 240),
        center,
        radius
    )

    pygame.draw.circle(
        screen,
        (30, 30, 30),
        center,
        radius,
        4
    )

    # Center point
    pygame.draw.circle(
        screen,
        (30, 30, 30),
        center,
        10
    )

    # --------------------------------------------------------
    # Pointer
    # --------------------------------------------------------

    pointer_length = radius * 0.85

    # Pygame y direction is downward,
    # therefore negative sin()
    rad = math.radians(angle - 90)

    end_x = center[0] + pointer_length * math.cos(rad)
    end_y = center[1] + pointer_length * math.sin(rad)

    pygame.draw.line(
        screen,
        (220, 40, 40),
        center,
        (end_x, end_y),
        10
    )

    # Arrow head
    arrow_size = 25

    left_angle = rad + math.radians(150)
    right_angle = rad - math.radians(150)

    left_point = (
        end_x + arrow_size * math.cos(left_angle),
        end_y + arrow_size * math.sin(left_angle)
    )

    right_point = (
        end_x + arrow_size * math.cos(right_angle),
        end_y + arrow_size * math.sin(right_angle)
    )

    pygame.draw.polygon(
        screen,
        (220, 40, 40),
        [
            (end_x, end_y),
            left_point,
            right_point
        ]
    )

    # --------------------------------------------------------
    # Text
    # --------------------------------------------------------

    angle_text = font.render(
        f"Angle: {angle:.1f} degrees",
        True,
        (20, 20, 20)
    )

    info_text = font.render(
        "Rotate M5AtomS3 around Z axis | R = reset",
        True,
        (60, 60, 60)
    )

    screen.blit(angle_text, (20, 20))
    screen.blit(info_text, (20, 55))

    pygame.display.flip()

    clock.tick(60)


ser.close()
pygame.quit()