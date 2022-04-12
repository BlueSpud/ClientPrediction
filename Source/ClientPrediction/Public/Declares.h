#pragma once

static constexpr Chaos::FReal kFixedDt = 0.0133333333;
static constexpr uint32 kInvalidFrame = -1;

// Authority

static constexpr uint32 kSyncFrames = 2;
static constexpr uint32 kAuthorityTargetInputBufferSize = 5;

// Auto proxy

static constexpr uint32 kInputWindowSize = 3;
static constexpr uint32 kClientForwardPredictionFrames = 5;

// Sim proxy

static constexpr float kDesiredInterpolationBufferMs = 100.0;