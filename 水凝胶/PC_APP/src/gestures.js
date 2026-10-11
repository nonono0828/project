const finger = (base, middle, tip) => [base, middle, tip];

export const POSES = Object.freeze({
  1: {
    name: '张开手掌', code: 'OPEN',
    index: finger(0, 0, 0), middle: finger(0, 0, 0), ring: finger(0, 0, 0), pinky: finger(0, 0, 0),
    thumb: finger(0, 0, 0), thumbSpread: 0
  },
  2: {
    name: '握拳', code: 'FIST',
    index: finger(58, 72, 52), middle: finger(62, 76, 55), ring: finger(64, 78, 56), pinky: finger(66, 80, 58),
    thumb: finger(-80, -40, -5), thumbSpread: -45, thumbTwist: 0, thumbLift: 0.6, thumbDepth: 0
  },
  3: {
    name: '胜利手势', code: 'VICTORY',
    index: finger(-5, 3, 2), middle: finger(-2, 2, 1), ring: finger(62, 76, 54), pinky: finger(66, 80, 58),
    thumb: finger(40, 46, 26), thumbSpread: 10
  },
  4: {
    name: '点赞', code: 'THUMBS UP',
    index: finger(62, 74, 54), middle: finger(64, 78, 56), ring: finger(66, 80, 58), pinky: finger(68, 82, 60),
    thumb: finger(-34, -5, 0), thumbSpread: -28
  },
  5: {
    name: 'OK手势', code: 'OK',
    index: finger(24, 58, 42), middle: finger(3, 4, 2), ring: finger(5, 6, 4), pinky: finger(8, 10, 6),
    thumb: finger(24, 38, 22), thumbSpread: 18
  },
  6: {
    name: '食指指向', code: 'POINT',
    index: finger(-4, 2, 1), middle: finger(62, 76, 55), ring: finger(65, 79, 57), pinky: finger(68, 82, 60),
    thumb: finger(34, 42, 24), thumbSpread: 8
  },
  7: {
    name: '捏合', code: 'PINCH',
    index: finger(18, 48, 35), middle: finger(20, 28, 16), ring: finger(24, 32, 20), pinky: finger(30, 38, 24),
    thumb: finger(28, 42, 26), thumbSpread: 20
  },
  8: {
    name: '摇滚手势', code: 'ROCK',
    index: finger(-4, 2, 1), middle: finger(64, 78, 56), ring: finger(66, 80, 58), pinky: finger(-6, 3, 2),
    thumb: finger(28, 34, 18), thumbSpread: 8
  },
  9: {
    name: '自然放松', code: 'RELAX',
    index: finger(8, 10, 7), middle: finger(6, 9, 6), ring: finger(9, 12, 8), pinky: finger(13, 16, 10),
    thumb: finger(8, 10, 6), thumbSpread: 2
  }
});
