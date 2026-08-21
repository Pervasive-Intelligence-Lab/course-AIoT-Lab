import serial
import re
import time
import pygame

from pygame.locals import DOUBLEBUF, OPENGL
from OpenGL.GL import *
from OpenGL.GLU import *


# =========================
# Serial
# =========================

SERIAL_PORT = "/dev/cu.usbmodem31101"   # Mac: to check the serial port, run `ls /dev/cu.*` in terminal
# Windows example: "COM5"
# Mac example: "/dev/cu.usbmodem31101"
BAUD_RATE = 115200

ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.01)

gyro_pattern = re.compile(
    r"gx:([-\d.]+)\s+gy:([-\d.]+)\s+gz:([-\d.]+)"
)


# =========================
# Orientation
# =========================

roll = 0.0
pitch = 0.0
yaw = 0.0

gx = 0.0
gy = 0.0
gz = 0.0

last_time = time.time()

GYRO_THRESHOLD = 0.5


# =========================
# Draw sphere
# =========================

def draw_sphere():

    quadric = gluNewQuadric()

    glColor3f(0.3, 0.65, 0.9)

    gluSphere(
        quadric,
        1.5,
        40,
        40
    )

    gluDeleteQuadric(quadric)


# =========================
# Draw pointer
# =========================

def draw_pointer():

    # 指针从球心指向球外
    glLineWidth(8)

    glColor3f(1.0, 0.1, 0.1)

    glBegin(GL_LINES)

    glVertex3f(0, 0, 0)
    glVertex3f(0, 0, 2.5)

    glEnd()

    # 箭头
    glPushMatrix()

    glTranslatef(0, 0, 2.5)

    glColor3f(1.0, 0.1, 0.1)

    quadric = gluNewQuadric()

    # cone
    gluCylinder(
        quadric,
        0.20,
        0.0,
        0.6,
        20,
        20
    )

    gluDeleteQuadric(quadric)

    glPopMatrix()


# =========================
# Draw axes
# =========================

def draw_axes():

    glLineWidth(3)

    glBegin(GL_LINES)

    # X
    glColor3f(1, 0, 0)
    glVertex3f(0, 0, 0)
    glVertex3f(2.5, 0, 0)

    # Y
    glColor3f(0, 1, 0)
    glVertex3f(0, 0, 0)
    glVertex3f(0, 2.5, 0)

    # Z
    glColor3f(0, 0, 1)
    glVertex3f(0, 0, 0)
    glVertex3f(0, 0, 2.5)

    glEnd()


# =========================
# OpenGL
# =========================

pygame.init()

display = (900, 700)

pygame.display.set_mode(
    display,
    DOUBLEBUF | OPENGL
)

pygame.display.set_caption("M5AtomS3 3D IMU Ball")

glEnable(GL_DEPTH_TEST)

glEnable(GL_BLEND)
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)

gluPerspective(
    45,
    display[0] / display[1],
    0.1,
    100.0
)

glTranslatef(0.0, 0.0, -8)

clock = pygame.time.Clock()


# =========================
# Main loop
# =========================

running = True

while running:

    for event in pygame.event.get():

        if event.type == pygame.QUIT:
            running = False

        if event.type == pygame.KEYDOWN:

            # R reset orientation
            if event.key == pygame.K_r:
                roll = 0
                pitch = 0
                yaw = 0

    # =====================
    # Read IMU
    # =====================

    while ser.in_waiting:

        try:

            line = ser.readline().decode(
                "utf-8",
                errors="ignore"
            ).strip()

            match = gyro_pattern.search(line)

            if match:

                gx = float(match.group(1))
                gy = float(match.group(2))
                gz = float(match.group(3))

                now = time.time()

                dt = now - last_time
                last_time = now

                if abs(gx) < GYRO_THRESHOLD:
                    gx = 0

                if abs(gy) < GYRO_THRESHOLD:
                    gy = 0

                if abs(gz) < GYRO_THRESHOLD:
                    gz = 0

                # Gyroscope integration
                roll += gx * dt
                pitch += gy * dt
                yaw += gz * dt

        except Exception as e:
            print(e)

    # =====================
    # Draw
    # =====================

    glClear(
        GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT
    )

    glPushMatrix()

    # Rotate whole sphere
    glRotatef(roll, 1, 0, 0)
    glRotatef(pitch, 0, 1, 0)
    glRotatef(yaw, 0, 0, 1)

    draw_sphere()

    draw_pointer()

    glPopMatrix()

    pygame.display.flip()

    clock.tick(60)


ser.close()
pygame.quit()