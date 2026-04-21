#include "pong_mode.h"
#include "drawing_engine.h"
#include "config.h"
#include <math.h>

// =====================================================
// Local frame builder for Pong
// =====================================================

static XYPoint g_frame[PONG_FRAME_MAX_POINTS];
static size_t g_frameCount = 0;

static inline void frameClear() {
  g_frameCount = 0;
}

static inline void framePush(uint16_t x, uint16_t y) {
  if (g_frameCount < PONG_FRAME_MAX_POINTS) {
    g_frame[g_frameCount++] = { x, y };
  }
}

static void frameAddLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t spacing = 10) {
  const int32_t dx = static_cast<int32_t>(x1) - static_cast<int32_t>(x0);
  const int32_t dy = static_cast<int32_t>(y1) - static_cast<int32_t>(y0);
  const int32_t maxAbs = max(abs(dx), abs(dy));
  int32_t steps = maxAbs / spacing;
  if (steps < 1) steps = 1;

  for (int32_t i = 0; i <= steps; ++i) {
    const uint16_t x = static_cast<uint16_t>(static_cast<int32_t>(x0) + (dx * i) / steps);
    const uint16_t y = static_cast<uint16_t>(static_cast<int32_t>(y0) + (dy * i) / steps);
    framePush(x, y);
  }
}

static void frameAddRectOutline(uint16_t xMin, uint16_t yMin, uint16_t xMax, uint16_t yMax, uint16_t spacing = 10) {
  frameAddLine(xMin, yMin, xMax, yMin, spacing);
  frameAddLine(xMax, yMin, xMax, yMax, spacing);
  frameAddLine(xMax, yMax, xMin, yMax, spacing);
  frameAddLine(xMin, yMax, xMin, yMin, spacing);
}

static void frameAddDigit(uint8_t digit, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  // Seven-segment style digit
  //   a
  // f   b
  //   g
  // e   c
  //   d

  const uint16_t x0 = x;
  const uint16_t x1 = x + w;
  // In XY mode for this project, larger DAC Y code maps upward on screen.
  // Build digits from a top anchor downward by subtracting height.
  const uint16_t y0 = y;
  const uint16_t y1 = y - h / 2;
  const uint16_t y2 = y - h;

  const bool segA[10] = {1,0,1,1,0,1,1,1,1,1};
  const bool segB[10] = {1,1,1,1,1,0,0,1,1,1};
  const bool segC[10] = {1,1,0,1,1,1,1,1,1,1};
  const bool segD[10] = {1,0,1,1,0,1,1,0,1,1};
  const bool segE[10] = {1,0,1,0,0,0,1,0,1,0};
  const bool segF[10] = {1,0,0,0,1,1,1,0,1,1};
  const bool segG[10] = {0,0,1,1,1,1,1,0,1,1};

  if (digit > 9) digit = 0;

  if (segA[digit]) frameAddLine(x0, y0, x1, y0, 8);
  if (segB[digit]) frameAddLine(x1, y0, x1, y1, 8);
  if (segC[digit]) frameAddLine(x1, y1, x1, y2, 8);
  if (segD[digit]) frameAddLine(x0, y2, x1, y2, 8);
  if (segE[digit]) frameAddLine(x0, y1, x0, y2, 8);
  if (segF[digit]) frameAddLine(x0, y0, x0, y1, 8);
  if (segG[digit]) frameAddLine(x0, y1, x1, y1, 8);
}

static void frameAddBall(uint16_t cx, uint16_t cy, uint16_t r) {
  frameAddRectOutline(cx - r, cy - r, cx + r, cy + r, 8);
}

// =====================================================
// Pong state
// =====================================================

enum class PongState : uint8_t {
  WAITING_TO_START,
  WAITING_FOR_SERVE,
  PLAYING,
  GAME_OVER
};

static PongState g_state = PongState::WAITING_TO_START;

static float g_leftPaddleY = DAC_CENTER_CODE;
static float g_rightPaddleY = DAC_CENTER_CODE;

static float g_ballX = DAC_CENTER_CODE;
static float g_ballY = DAC_CENTER_CODE;
static float g_ballVX = 0.0f;
static float g_ballVY = 0.0f;

static uint8_t g_leftScore = 0;
static uint8_t g_rightScore = 0;

static uint32_t g_lastUpdateMs = 0;

// The Pong layout was tuned for the earlier ~2320-code draw span.
// Re-scale all spatial values when the usable analog window changes.
static constexpr float PONG_REFERENCE_SPAN = 2320.0f;
static constexpr float PONG_LAYOUT_SCALE =
  static_cast<float>(DRAW_MAX_CODE - DRAW_MIN_CODE) / PONG_REFERENCE_SPAN;

static inline uint16_t scaleU16(float base) {
  return static_cast<uint16_t>(lroundf(base * PONG_LAYOUT_SCALE));
}

static inline float scaleF(float base) {
  return base * PONG_LAYOUT_SCALE;
}

// Geometry
static const uint16_t PADDLE_INSET = scaleU16(180.0f);
static const uint16_t PADDLE_HALF_HEIGHT = scaleU16(180.0f);
static const uint16_t PADDLE_MOVE_PER_COUNT = scaleU16(10.0f);
static const uint16_t BALL_RADIUS = scaleU16(35.0f);
static const uint16_t PADDLE_COLLISION_HALF_WIDTH = scaleU16(40.0f);
static const float BALL_SERVE_VX = scaleF(18.0f);
static const float BALL_SERVE_VY = scaleF(7.0f);
static const float BALL_HIT_VX_BONUS = scaleF(0.6f);
static const float BALL_HIT_VY_SCALE = scaleF(16.0f);

// Playfield
static const uint16_t FIELD_MARGIN = scaleU16(120.0f);
static const uint16_t FIELD_LEFT   = DRAW_MIN_CODE + FIELD_MARGIN;
static const uint16_t FIELD_RIGHT  = DRAW_MAX_CODE - FIELD_MARGIN;
static const uint16_t FIELD_TOP    = DRAW_MAX_CODE - FIELD_MARGIN;
static const uint16_t FIELD_BOTTOM = DRAW_MIN_CODE + FIELD_MARGIN;

static const uint16_t LEFT_PADDLE_X  = FIELD_LEFT + PADDLE_INSET;
static const uint16_t RIGHT_PADDLE_X = FIELD_RIGHT - PADDLE_INSET;

static inline float paddleMinY() { return FIELD_BOTTOM + PADDLE_HALF_HEIGHT; }
static inline float paddleMaxY() { return FIELD_TOP - PADDLE_HALF_HEIGHT; }

static float clampPaddleY(float y) {
  if (y < paddleMinY()) return paddleMinY();
  if (y > paddleMaxY()) return paddleMaxY();
  return y;
}

static void pongResetPositions() {
  g_leftPaddleY = DAC_CENTER_CODE;
  g_rightPaddleY = DAC_CENTER_CODE;
  g_ballX = DAC_CENTER_CODE;
  g_ballY = DAC_CENTER_CODE;
  g_ballVX = 0.0f;
  g_ballVY = 0.0f;
}

static void pongStartNewMatch() {
  g_leftScore = 0;
  g_rightScore = 0;
  pongResetPositions();
  g_state = PongState::WAITING_FOR_SERVE;
}

static void pongServe(int direction) {
  g_ballX = DAC_CENTER_CODE;
  g_ballY = DAC_CENTER_CODE;

  g_ballVX = (direction >= 0) ? BALL_SERVE_VX : -BALL_SERVE_VX;
  g_ballVY = BALL_SERVE_VY;

  g_state = PongState::PLAYING;
}

static void pongAwardPointLeft() {
  if (g_leftScore < 9) g_leftScore++;
  if (g_leftScore >= 5) {
    g_state = PongState::GAME_OVER;
    pongResetPositions();
  } else {
    pongResetPositions();
    g_state = PongState::WAITING_FOR_SERVE;
  }
}

static void pongAwardPointRight() {
  if (g_rightScore < 9) g_rightScore++;
  if (g_rightScore >= 5) {
    g_state = PongState::GAME_OVER;
    pongResetPositions();
  } else {
    pongResetPositions();
    g_state = PongState::WAITING_FOR_SERVE;
  }
}

static void pongBuildFrame() {
  frameClear();

  // Left paddle
  frameAddLine(
    LEFT_PADDLE_X,
    static_cast<uint16_t>(g_leftPaddleY - PADDLE_HALF_HEIGHT),
    LEFT_PADDLE_X,
    static_cast<uint16_t>(g_leftPaddleY + PADDLE_HALF_HEIGHT),
    8
  );

  // Right paddle
  frameAddLine(
    RIGHT_PADDLE_X,
    static_cast<uint16_t>(g_rightPaddleY - PADDLE_HALF_HEIGHT),
    RIGHT_PADDLE_X,
    static_cast<uint16_t>(g_rightPaddleY + PADDLE_HALF_HEIGHT),
    8
  );

  // Ball
  frameAddBall(static_cast<uint16_t>(g_ballX), static_cast<uint16_t>(g_ballY), BALL_RADIUS);

  // Scores
  const uint16_t digitW = scaleU16(90.0f);
  const uint16_t digitH = scaleU16(180.0f);
  const uint16_t gap = scaleU16(70.0f);
  const uint16_t centerX = DAC_CENTER_CODE;
  const uint16_t scoreY = FIELD_TOP - scaleU16(220.0f);

  frameAddDigit(g_leftScore,  centerX - gap - digitW, scoreY, digitW, digitH);
  frameAddDigit(g_rightScore, centerX + gap,          scoreY, digitW, digitH);

  drawingSetPongFrame(g_frame, g_frameCount);
}

void pongBegin() {
  g_lastUpdateMs = millis();
  g_state = PongState::WAITING_TO_START;
  pongResetPositions();
  pongBuildFrame();
}

void pongOnEnter() {
  // Rebuild visible frame when entering mode
  pongBuildFrame();
}

void pongUpdate(const InputSnapshot& in) {
  // Paddle motion always active while in Pong mode
  // Board wiring reports encoders crossed for Pong controls,
  // so swap deltas to keep left knob -> left paddle and right knob -> right paddle.
  int32_t leftDelta = in.rightEncoderDelta;
  int32_t rightDelta = in.leftEncoderDelta;

  // In Pong, each paddle can be inverted independently.
  if (INVERT_PONG_LEFT_PADDLE) {
    leftDelta = -leftDelta;
  }
  if (INVERT_PONG_RIGHT_PADDLE) {
    rightDelta = -rightDelta;
  }

  g_leftPaddleY = clampPaddleY(g_leftPaddleY + leftDelta * PADDLE_MOVE_PER_COUNT);
  g_rightPaddleY = clampPaddleY(g_rightPaddleY + rightDelta * PADDLE_MOVE_PER_COUNT);

  // S2: start / serve / restart
  if (in.resetPressedEdge) {
    if (g_state == PongState::WAITING_TO_START) {
      pongStartNewMatch();
    } else if (g_state == PongState::WAITING_FOR_SERVE) {
      const int serveDir = ((g_leftScore + g_rightScore) & 1) ? -1 : 1;
      pongServe(serveDir);
    } else if (g_state == PongState::GAME_OVER) {
      pongStartNewMatch();
    }
  }

  const uint32_t now = millis();
  if ((now - g_lastUpdateMs) < 8) {
    pongBuildFrame();
    return;
  }
  g_lastUpdateMs = now;

  if (g_state != PongState::PLAYING) {
    pongBuildFrame();
    return;
  }

  g_ballX += g_ballVX;
  g_ballY += g_ballVY;

  // Top/bottom bounce
  if (g_ballY + BALL_RADIUS >= FIELD_TOP) {
    g_ballY = FIELD_TOP - BALL_RADIUS;
    g_ballVY = -fabsf(g_ballVY);
  }
  if (g_ballY - BALL_RADIUS <= FIELD_BOTTOM) {
    g_ballY = FIELD_BOTTOM + BALL_RADIUS;
    g_ballVY = fabsf(g_ballVY);
  }

  // Left paddle collision
  if (g_ballVX < 0 &&
      g_ballX - BALL_RADIUS <= LEFT_PADDLE_X &&
      g_ballX - BALL_RADIUS >= LEFT_PADDLE_X - PADDLE_COLLISION_HALF_WIDTH &&
      g_ballY >= g_leftPaddleY - PADDLE_HALF_HEIGHT &&
      g_ballY <= g_leftPaddleY + PADDLE_HALF_HEIGHT) {

    g_ballX = LEFT_PADDLE_X + BALL_RADIUS;

    float relative = (g_ballY - g_leftPaddleY) / static_cast<float>(PADDLE_HALF_HEIGHT);
    if (relative < -1.0f) relative = -1.0f;
    if (relative >  1.0f) relative =  1.0f;

    g_ballVX = fabsf(g_ballVX) + BALL_HIT_VX_BONUS;
    g_ballVY = relative * BALL_HIT_VY_SCALE;
  }

  // Right paddle collision
  if (g_ballVX > 0 &&
      g_ballX + BALL_RADIUS >= RIGHT_PADDLE_X &&
      g_ballX + BALL_RADIUS <= RIGHT_PADDLE_X + PADDLE_COLLISION_HALF_WIDTH &&
      g_ballY >= g_rightPaddleY - PADDLE_HALF_HEIGHT &&
      g_ballY <= g_rightPaddleY + PADDLE_HALF_HEIGHT) {

    g_ballX = RIGHT_PADDLE_X - BALL_RADIUS;

    float relative = (g_ballY - g_rightPaddleY) / static_cast<float>(PADDLE_HALF_HEIGHT);
    if (relative < -1.0f) relative = -1.0f;
    if (relative >  1.0f) relative =  1.0f;

    g_ballVX = -(fabsf(g_ballVX) + BALL_HIT_VX_BONUS);
    g_ballVY = relative * BALL_HIT_VY_SCALE;
  }

  // Scoring
  if (g_ballX < FIELD_LEFT) {
    pongAwardPointRight();
  } else if (g_ballX > FIELD_RIGHT) {
    pongAwardPointLeft();
  }

  pongBuildFrame();
}