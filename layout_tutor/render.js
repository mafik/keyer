// Rendering function for the tutor interface
// Uses global state variables from state.js and tutor.js

// Convert RGB (0-255) to HSL (h: 0-360, s: 0-100, l: 0-100)
function rgbToHsl(r, g, b) {
  r /= 255;
  g /= 255;
  b /= 255;

  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);
  const delta = max - min;

  let h = 0,
    s = 0,
    l = (max + min) / 2;

  if (delta !== 0) {
    s = l > 0.5 ? delta / (2 - max - min) : delta / (max + min);

    switch (max) {
      case r:
        h = ((g - b) / delta + (g < b ? 6 : 0)) / 6;
        break;
      case g:
        h = ((b - r) / delta + 2) / 6;
        break;
      case b:
        h = ((r - g) / delta + 4) / 6;
        break;
    }
  }

  return [h * 360, s * 100, l * 100];
}

// Convert HSL back to RGB hex
function hslToRgbHex(h, s, l) {
  s /= 100;
  l /= 100;

  const c = (1 - Math.abs(2 * l - 1)) * s;
  const x = c * (1 - Math.abs(((h / 60) % 2) - 1));
  const m = l - c / 2;

  let r = 0,
    g = 0,
    b = 0;

  if (h >= 0 && h < 60) {
    r = c;
    g = x;
    b = 0;
  } else if (h >= 60 && h < 120) {
    r = x;
    g = c;
    b = 0;
  } else if (h >= 120 && h < 180) {
    r = 0;
    g = c;
    b = x;
  } else if (h >= 180 && h < 240) {
    r = 0;
    g = x;
    b = c;
  } else if (h >= 240 && h < 300) {
    r = x;
    g = 0;
    b = c;
  } else if (h >= 300 && h < 360) {
    r = c;
    g = 0;
    b = x;
  }

  r = Math.round((r + m) * 255);
  g = Math.round((g + m) * 255);
  b = Math.round((b + m) * 255);

  return "#" + [r, g, b].map((x) => x.toString(16).padStart(2, "0")).join("");
}

// Darken a hex color by reducing lightness in HSL space
function setLightness(hexColor, lightness = 0.3) {
  // Parse hex
  const r = parseInt(hexColor.slice(1, 3), 16);
  const g = parseInt(hexColor.slice(3, 5), 16);
  const b = parseInt(hexColor.slice(5, 7), 16);

  // Convert to HSL
  let [h, s, l] = rgbToHsl(r, g, b);

  // Reduce lightness
  l = Math.max(0, lightness * 100);

  // Convert back to hex
  return hslToRgbHex(h, s, l);
}

// Returns the appropriate key color based on action
// action: single-digit string ("1", "2", or "3")
// dark: if true, returns dark variant (for outlines), otherwise returns normal color
// Returns: hex color string (e.g., "#c2b280")
function getKeyColor(action, dark = false) {
  // Base colors
  let color;
  if (action === "1") {
    color = "#c2b280"; // Sandy desert (194, 178, 128)
  } else if (action === "2") {
    color = "#5d6532"; // Olive green (93, 101, 50)
  } else if (action === "3") {
    color = "#705843"; // Brown (112, 88, 67)
  } else {
    color = "#c2b280"; // Default fallback
  }

  // Return darkened version if requested
  if (dark) {
    return setLightness(color, 0.15);
  }

  return color;
}

// Canvas - private to render.js
let canvas;
let ctx;

// Animation state
let animationFrameId = null;

// Fingerplan Z animation state
let currentZOffset = 0; // Current animated Z offset
let zVelocity = 0; // Z velocity for smooth animation
let lastAnimationTime = null;

// Tachometer animation state
let tachometerLevel = 0; // Current animated tachometer level (starts at 0)
let tachometerVelocity = 0; // Velocity for smooth animation

let accuracyValue = 0;
let accuracyVelocity = 0;

let wpmValue = 0;
let wpmVelocity = 0;

function initRender() {
  canvas = document.getElementById("canvas");
  ctx = canvas.getContext("2d");
  resizeCanvas();

  // Wait for the Modern Typewriter font to load before rendering
  document.fonts.ready.then(() => {
    render();
  });

  // Handle window resize
  window.addEventListener("resize", () => {
    resizeCanvas();
    render();
  });
}

function resizeCanvas() {
  const dpr = window.devicePixelRatio || 1;
  const width = window.innerWidth;
  const height = window.innerHeight;

  // Set canvas size accounting for device pixel ratio
  canvas.width = width * dpr;
  canvas.height = height * dpr;

  // Scale canvas CSS size back to logical size
  canvas.style.width = width + "px";
  canvas.style.height = height + "px";

  // Scale context to account for device pixel ratio
  ctx.scale(dpr, dpr);
}

function render() {
  const width = window.innerWidth;
  const height = window.innerHeight;

  // Clear canvas with transparency
  ctx.clearRect(0, 0, width, height);

  // Calculate scale for learning sequence and chord grid
  const charWidth = 30;
  const requiredWidth = learning_sequence.length * charWidth;
  const maxWidth = width - 40; // Leave 20px margin on each side

  // Calculate height constraint: learning sequence (50 + text height) + chord grid (80 + 5*25)
  const sequenceAndGridHeight = 80 + 5 * 25;
  const maxHeight = (height * 2) / 5;

  // Apply both width and height scaling constraints
  let scale = 1;
  if (requiredWidth > maxWidth) {
    scale = maxWidth / requiredWidth;
  }
  if (sequenceAndGridHeight * scale > maxHeight) {
    scale = Math.min(scale, maxHeight / sequenceAndGridHeight);
  }

  // Animate tachometer using SineApproach
  const targetLevel = currentLevel();
  const wpm = getCurrentWPM();
  const targetAccuracy = getCurrentAccuracy();
  const tachometerPeriod = 3.0; // 3 second animation period
  const delta_t = 1 / 60; // Fixed 60 FPS timestep

  [tachometerLevel, tachometerVelocity] = SineApproach(
    tachometerLevel,
    tachometerVelocity,
    tachometerPeriod,
    targetLevel,
    delta_t,
  );
  [accuracyValue, accuracyVelocity] = SineApproach(
    accuracyValue,
    accuracyVelocity,
    0.5,
    targetAccuracy,
    delta_t,
  );
  [wpmValue, wpmVelocity] = SineApproach(
    wpmValue,
    wpmVelocity,
    0.5,
    wpm,
    delta_t,
  );

  const velocityThreshold = 0.01;
  let shouldContinueAnimation =
    Math.abs(zVelocity) >= velocityThreshold ||
    Math.abs(tachometerVelocity) >= velocityThreshold ||
    Math.abs(accuracyVelocity) >= velocityThreshold ||
    Math.abs(wpmVelocity) >= velocityThreshold;

  // Render stats
  {
    // Render vertical WPM bar on left
    renderWPMBar(ctx, height, wpmValue);

    // Render vertical accuracy bar on right
    renderAccuracyBar(ctx, width, height, accuracyValue);

    ctx.textAlign = "left";
  }

  // Save context and apply scaling if needed
  ctx.save();
  if (scale < 1) {
    // Scale from center horizontally
    ctx.translate(width / 2, 0);
    ctx.scale(scale, scale);
    ctx.translate(-width / 2, 0);
  }

  // Render learning sequence at top
  renderLearningSequence(ctx, width);

  // Render chord grid below learning sequence
  renderChordGrid(ctx, width);

  ctx.restore();

  renderTachometer(ctx, scale);

  // Render fingerplan
  renderFingerplan(ctx, width, height);

  // Render main text area
  renderTextArea(ctx, width, height);

  // Render debug logs
  // disabled
  // renderDebugLogs(ctx, width, height);

  if (lastCharTime !== null) {
    const timeSinceLastChar = (Date.now() - lastCharTime) / 1000;
    if (timeSinceLastChar < 5) {
      shouldContinueAnimation = true;
    }
  }

  // Request next frame if we should continue
  if (shouldContinueAnimation) {
    if (animationFrameId === null) {
      animationFrameId = requestAnimationFrame(animateFrame);
    }
  }
}

function animateFrame() {
  animationFrameId = null;
  render();
}

function renderLearningSequence(ctx, width) {
  ctx.font = "24px 'Modern Typewriter', monospace";
  const charWidth = 30;
  const startX = width / 2 - (learning_sequence.length * charWidth) / 2 + 40;
  const y = 50;

  // Characters
  ctx.textAlign = "center";
  for (let i = 0; i < learning_sequence.length; i++) {
    const char = learning_sequence[i];
    const x = startX + i * charWidth;

    // Highlight current learning characters
    if (i === newIndex) {
      ctx.fillStyle = "#4ec9b0";
      ctx.fillText("▼", x, y - 15);
      ctx.fillStyle = "#4ec9b0";
    } else if (i === oldIndex && oldIndex !== newIndex) {
      ctx.fillStyle = "#ce9178";
      ctx.fillText("▼", x, y - 15);
      ctx.fillStyle = "#ce9178";
    } else if (i < newIndex) {
      ctx.fillStyle = "#6a9955"; // Learned
    } else {
      ctx.fillStyle = "#3e3e42"; // Not yet learned
    }
    ctx.fillText(char === " " ? "␣" : char, x, y);
  }

  ctx.textAlign = "left";
}

// Render level label and tachometer in upper left corner
function renderTachometer(ctx, scale) {
  ctx.save();
  ctx.scale(scale, scale);
  let x = 7;
  const y = 25; // Top margin
  ctx.fillStyle = "#5D6532";
  ctx.beginPath();
  ctx.roundRect(-10, -10, 70, 80, 10);
  ctx.filter = "drop-shadow(0px 0px 8px rgba(0, 0, 0, 0.5))";
  ctx.fill();
  ctx.filter = "none";
  const barrelWidth = 17; // Width of each digit barrel
  const barrelHeight = 25; // Height of each barrel
  const barrelSpacing = -1; // Space between barrels
  ctx.fillStyle = "#d4d4d4";
  ctx.font = "15px 'Bunker Stencil', monospace";
  ctx.textAlign = "left";
  ctx.fillText("LEVEL", x, y - 5);
  ctx.font = "10px 'Bunker Stencil', monospace";
  ctx.fillText("max " + maxLevel(), x, y + barrelHeight + 11);

  // Calculate rotation for each barrel
  // Ones barrel: wraps at 10, includes fractional part
  const onesRotation = tachometerLevel % 10;
  // Tens barrel: based on floor of level / 10, plus fractional part from ones crossing 10
  let tensRotation = Math.floor(tachometerLevel / 10) % 10;
  if (onesRotation > 9) {
    tensRotation += onesRotation - 9;
  }
  let hundredsRotation = Math.floor(tachometerLevel / 100) % 10;
  if (tensRotation > 9) {
    hundredsRotation += tensRotation - 9;
  }
  x -= 2;

  drawBarrel(ctx, x, y, barrelWidth, barrelHeight, hundredsRotation);
  x += barrelWidth + barrelSpacing;
  drawBarrel(ctx, x, y, barrelWidth, barrelHeight, tensRotation);
  x += barrelWidth + barrelSpacing;
  drawBarrel(ctx, x, y, barrelWidth, barrelHeight, onesRotation);
  ctx.restore();
}

function renderChordGrid(ctx, width) {
  const charWidth = 30;
  const startX = width / 2 - (learning_sequence.length * charWidth) / 2 + 40;
  const gridStartY = 80;
  const lineSpacing = 25;
  const circleRadius = 8;

  const fingerNames = ["Thumb", "Index", "Middle", "Ring", "Little"];

  // Draw horizontal lines for each finger
  for (let finger = 0; finger < 5; finger++) {
    const y = gridStartY + finger * lineSpacing;

    // Draw line
    ctx.strokeStyle = "#3e3e42";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(startX - 15, y);
    ctx.lineTo(startX + learning_sequence.length * charWidth - 15, y);
    ctx.stroke();

    // Draw finger label on the left
    ctx.fillStyle = "#858585";
    ctx.font = "12px 'Modern Typewriter', monospace";
    ctx.textAlign = "right";
    ctx.fillText(fingerNames[finger], startX - 25, y + 4);
  }

  ctx.textAlign = "center";

  // Draw circles for each character's chord
  for (let i = 0; i < learning_sequence.length; i++) {
    const char = learning_sequence[i];
    const chord = layout[char];
    const x = startX + i * charWidth;

    if (!chord) continue;

    // Determine color based on position
    let fillColor;
    let strokeColor;
    if (i === newIndex) {
      fillColor = "#4ec9b0"; // Cyan for NEW
    } else if (i === oldIndex && oldIndex !== newIndex) {
      fillColor = "#ce9178"; // Orange for OLD
    } else if (i < newIndex) {
      fillColor = "#6a9955"; // Green for learned
    } else {
      fillColor = "#858585"; // Dark gray for not yet learned
    }

    // chord format starts with Thumb: chord[0]=Thumb, chord[1]=Index, etc.
    // Display order: Thumb at top (row 0), Little at bottom (row 4)
    // So they map directly: display row = chord index

    for (let finger = 0; finger < 5; finger++) {
      const y = gridStartY + finger * lineSpacing;
      const position = chord[finger];

      if (position !== "0") {
        renderAction(ctx, x, y + circleRadius, position, {
          height: circleRadius * 2,
          fill: fillColor,
          strokeWidth: 2,
          strokeColor: "#3e3e42",
        });
      }
    }
  }

  ctx.textAlign = "left";
}

function renderTextArea(ctx, width, height) {
  const baseFontSize = 48;
  const maxWidth = width - 120; // Leave margin on both sides

  // Set initial font to measure text
  ctx.font = `${baseFontSize}px 'Zrnic'`;

  // Measure the actual width of the text
  const displayText = targetText.replace(/ /g, "␣");
  const textMetrics = ctx.measureText(displayText);
  const requiredWidth = textMetrics.width;

  // Calculate scale if text is too wide
  let scale = 1;
  if (requiredWidth > maxWidth) {
    scale = maxWidth / requiredWidth;
  }

  const fontSize = baseFontSize * scale;
  ctx.font = `${fontSize}px 'Modern Typewriter'`;

  // Measure all prefix widths for proper positioning (non-monospace)
  // Array will be length displayText.length + 1
  // measuredWidths[0] = 0, measuredWidths[i] = width of displayText[0..i-1]
  const measuredWidths = [0];
  for (let i = 1; i <= displayText.length; i++) {
    const prefix = displayText.substring(0, i);
    const prefixWidth = ctx.measureText(prefix).width;
    measuredWidths.push(prefixWidth);
  }

  const startY = height / 2 + 24 * scale;

  // Calculate text positioning
  const totalWidth = measuredWidths[displayText.length];
  const startX = width / 2 - totalWidth / 2;

  // Draw radar display background
  const padding = 30;
  const radarX = startX - padding;
  const radarY = startY - 60 * scale;
  const radarWidth = totalWidth + padding * 2;
  const radarHeight = 80 * scale;
  const radarRadius = 35;

  // Outer shadow for depth
  ctx.save();
  ctx.shadowColor = "rgba(0, 0, 0, 0.8)";
  ctx.shadowBlur = 20;
  ctx.shadowOffsetX = 0;
  ctx.shadowOffsetY = 8;

  // Draw main background with gradient
  const bgGradient = ctx.createRadialGradient(
    width / 2,
    height / 2,
    0,
    width / 2,
    height / 2,
    radarWidth / 2,
  );
  bgGradient.addColorStop(0, "#0a0f0a");
  bgGradient.addColorStop(1, "#050805");

  ctx.fillStyle = bgGradient;
  ctx.beginPath();
  ctx.roundRect(radarX, radarY, radarWidth, radarHeight, radarRadius);
  ctx.fill();
  ctx.restore();

  // Inner glow effect
  ctx.save();
  ctx.strokeStyle = "rgba(0, 255, 100, 0.4)";
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.roundRect(
    radarX + 5,
    radarY + 5,
    radarWidth - 10,
    radarHeight - 10,
    radarRadius - 3,
  );
  ctx.stroke();
  ctx.restore();

  // Render each character with green radar display colors
  for (let i = 0; i < targetText.length; i++) {
    const char = targetText[i];
    const x = startX + measuredWidths[i];

    if (i < typedText.length) {
      // Already typed
      if (typedText[i] === char) {
        ctx.fillStyle = "#004400"; // Correct - dark green
      } else {
        ctx.fillStyle = "#ff4444"; // Incorrect - red
      }
    } else if (i === typedText.length || i === typedText.length + 1) {
      // Current character and next character to type
      ctx.fillStyle = "#00ff66"; // Bright green highlight
      // Draw cursor only for the current character (first of the two)
      if (i === typedText.length) {
        // Calculate overline dimensions spanning current and next character
        const twoCharWidth =
          i + 2 <= measuredWidths.length - 1
            ? measuredWidths[i + 2] - measuredWidths[i]
            : measuredWidths[i + 1] - measuredWidths[i];
        const overlineHeight = 3;
        const overlineOffset = 45 * scale;

        // Cursor as overline with green glow
        ctx.save();
        ctx.shadowColor = "#00ff66";
        ctx.shadowBlur = 10;
        ctx.fillStyle = "#00ff66";
        ctx.strokeStyle = "#00ff66";
        ctx.lineWidth = overlineHeight;
        ctx.beginPath();
        ctx.moveTo(x, startY - overlineOffset);
        ctx.lineTo(x + twoCharWidth, startY - overlineOffset);
        ctx.moveTo(x + twoCharWidth / 2, startY - overlineOffset);
        ctx.lineTo(x + twoCharWidth / 2, height * 0.4);
        ctx.lineTo(width * 0.2, height * 0.4);
        // ctx.lineTo(100, startY - overlineOffset);
        ctx.stroke();
        ctx.restore();
      }
    } else {
      // Not yet typed
      ctx.fillStyle = "#004400"; // Dark green
    }

    // Add slight glow to text
    ctx.save();
    ctx.shadowColor = "rgba(0, 255, 100, 0.5)";
    ctx.shadowBlur = 3;
    ctx.fillText(char === " " ? "␣" : char, x, startY);
    ctx.restore();
  }
}

function SineApproach(value, velocity, period, target, delta_t) {
  // x = t
  // P1 = 2 * PI / period
  // y = a * (1 - sin(x * P1))
  // y' = -a * P1 * cos(x * P1)

  let P1 = (2 * Math.PI) / Number(period);
  let y = value - target;
  let v = Number(velocity);

  let a;
  if (Math.abs(velocity) < 1e-6) {
    a = y / 2;
  } else {
    a = ((v * v) / P1 / P1 + y * y) / y / 2;
  }

  if (Math.abs(a) > Math.abs(y)) {
    a = y;
  }

  if (v < -Math.abs(a) * P1) {
    v = -Math.abs(a) * P1;
  } else if (v > Math.abs(a) * P1) {
    v = Math.abs(a) * P1;
  }
  let fract = Math.abs((a * P1 + v) / (a * P1 - v));
  let x;
  if (isNaN(fract)) {
    x = 0;
  } else {
    x = -2 * Math.atan(Math.sqrt(fract));
  }

  if (x > Math.PI / 2) x -= Math.PI * 2;
  x += delta_t * P1;
  if (x > Math.PI / 2) x = Math.PI / 2;

  return [a * (1 - Math.sin(x)) + target, -a * P1 * Math.cos(x)];
}

function drawBarrel(ctx, x, y, width, height, rotation) {
  rotation = Math.max(rotation, 0);
  // Save context
  ctx.save();

  // Create clipping region for the barrel
  ctx.beginPath();
  ctx.rect(x, y, width, height);
  ctx.clip();

  // Draw barrel background with vertical gradient to show curvature
  const gradient = ctx.createLinearGradient(x, y, x, y + height);
  gradient.addColorStop(0, "#1a1a1a");
  gradient.addColorStop(0.5, "#2a2a2a");
  gradient.addColorStop(1, "#1a1a1a");
  ctx.fillStyle = gradient;
  ctx.fillRect(x, y, width, height);

  // Each digit takes up the full height of the window
  const digitHeight = height;

  // Set up text drawing
  ctx.font = width + "px 'Modern Typewriter', monospace";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillStyle = "#ffffff";

  // Draw all 10 digits in a continuous column
  // rotation represents the full position (e.g., 0, 0.5, 1, 1.5, ..., 9, 9.5, 10, ...)
  // We draw extra digits above and below to handle wrapping smoothly
  for (let i = -1; i <= 11; i++) {
    let digit = i % 10;
    // Position each digit relative to rotation
    const digitY = y + height / 2 - (i - rotation) * digitHeight;

    ctx.fillText(digit.toString(), x + width / 2, digitY);
  }

  // Draw barrel border
  ctx.strokeStyle = "#101010";
  ctx.lineWidth = 2;
  ctx.strokeRect(x, y, width, height);

  ctx.restore();
}

// Renders an outlined action letter (1, 2, or 3) at the given position
// x, y: center position in canvas coordinates
// action: single-digit string ("1", "2", or "3")
// options: { width?: number, height?: number, fill?: string }
//   - width: desired width in pixels (total width including stroke) - uses measureText
//   - height: desired height in pixels - scales font size directly
//   - must specify exactly one of width or height
//   - fill: fill color (default: "bright key color")
function renderAction(ctx, x, y, action, options) {
  let fontSize;
  let strokeWidth;

  if (options.height !== undefined) {
    // Height-based: scale font size directly
    fontSize = options.height;
    if (options.strokeWidth !== undefined) {
      strokeWidth = options.strokeWidth;
    } else {
      strokeWidth = fontSize * 0.2;
    }
  } else if (options.width !== undefined) {
    // Width-based: use measureText
    const width = options.width;

    const baseFontSize = 20;
    ctx.font = `${baseFontSize}px 'Bunker Stencil', monospace`;
    const baseMetrics = ctx.measureText(action);
    const baseWidth = baseMetrics.width;

    if (options.strokeWidth !== undefined) {
      strokeWidth = options.strokeWidth;
    } else {
      strokeWidth = width * 0.2;
    }
    const targetTextWidth = width - strokeWidth;
    fontSize = (targetTextWidth / baseWidth) * baseFontSize;
  } else {
    throw new Error("renderAction: must specify either width or height");
  }

  const fillColor =
    options.fill || setLightness(getKeyColor(action, false), 0.8);

  const strokeColor = options.strokeColor || getKeyColor(action, true);

  ctx.font = `${fontSize}px 'Bunker Stencil', monospace`;
  ctx.textAlign = "center";
  ctx.textBaseline = "bottom";

  if (strokeWidth) {
    ctx.strokeStyle = strokeColor;
    ctx.lineWidth = strokeWidth;
    ctx.strokeText(action, x, y);
  }

  // Draw fill
  ctx.fillStyle = fillColor;
  ctx.fillText(action, x, y);
}

function renderFingerplan(ctx, width, height) {
  // Get fingerplan for target text
  const plan = fingerPlan(targetText);
  if (!plan || plan.length === 0) return;

  const numFingers = 5;
  const marginSide = 60; // Space for WPM/accuracy bars
  const bottomY = height; // Bottom of screen
  const centerY = height / 2; // Center of screen (vanishing point)

  // Calculate finger line positions at bottom
  const fingerSpacing = (width - 2 * marginSide) / (numFingers + 1);
  const fingerBottomX = [];
  for (let i = 0; i < numFingers; i++) {
    fingerBottomX[i] = marginSide + (i + 1) * fingerSpacing;
  }

  // Center X for convergence
  const centerX = width / 2;

  // 3D world space parameters
  const nearZ = 1.15; // Near plane distance from camera (units in 3D space) - offset to prevent clipping
  const worldSpacePerEvent = 0.3; // Distance between events in 3D world space
  const screenBottom = bottomY; // Screen position of near plane
  const vanishingPoint = centerY; // Vanishing point (horizon)

  // Calculate target Z offset based on typed text
  const targetZOffset = typedText.length * 2 * worldSpacePerEvent;

  // Animate Z offset using SineApproach with fixed timestep
  const period = 0.5; // 0.5 second period for responsive animation
  const delta_t = 1 / 60; // Fixed 60 FPS timestep

  {
    const startZ =
      nearZ +
      (typedText.length * 2 + 0.5) * worldSpacePerEvent -
      currentZOffset;
    const endZ =
      nearZ +
      (typedText.length * 2 + 2.5) * worldSpacePerEvent -
      currentZOffset;
    const midZ =
      nearZ +
      (typedText.length * 2 + 1.5) * worldSpacePerEvent -
      currentZOffset;
    const startY = vanishingPoint + (screenBottom - vanishingPoint) / startZ;
    const midY = vanishingPoint + (screenBottom - vanishingPoint) / midZ;
    const endY = vanishingPoint + (screenBottom - vanishingPoint) / endZ;

    const midPerspectiveFactor = 1 / midZ;
    const endPerspectiveFactor = 1 / endZ;
    const startPerspectiveFactor = 1 / startZ;

    const plateWidth = width * 0.4;

    ctx.save();
    ctx.shadowColor = "#00ff66";
    ctx.shadowBlur = 10;
    ctx.fillStyle = "#00ff66";
    ctx.strokeStyle = "#00ff66";
    ctx.lineWidth = 3;
    ctx.beginPath();
    ctx.moveTo(width * 0.2 + 3, height * 0.4);
    ctx.lineTo(width * 0.2, height * 0.4);
    if (endZ > nearZ) {
      ctx.lineTo(width * 0.2, midY);
      ctx.lineTo(centerX - plateWidth * midPerspectiveFactor, midY);
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(centerX - plateWidth * endPerspectiveFactor, endY);
      ctx.lineTo(centerX - plateWidth * startPerspectiveFactor, startY);
      ctx.lineTo(centerX + plateWidth * startPerspectiveFactor, startY);
      ctx.lineTo(centerX + plateWidth * endPerspectiveFactor, endY);
      ctx.lineTo(centerX - plateWidth * endPerspectiveFactor, endY);
      ctx.fillStyle = "#0a0f0a";
      ctx.fill();
      ctx.stroke();
    } else {
      ctx.lineTo(width * 0.2, height);
      ctx.stroke();
    }
    ctx.restore();
  }

  // Draw perspective lines for each finger
  for (let i = 0; i < numFingers; i++) {
    const gradient = ctx.createLinearGradient(
      fingerBottomX[i],
      bottomY,
      centerX,
      centerY,
    );
    gradient.addColorStop(0, "rgba(200, 200, 200, 0.3)");
    gradient.addColorStop(1, "rgba(200, 200, 200, 0)");

    ctx.strokeStyle = gradient;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(fingerBottomX[i], bottomY);
    ctx.lineTo(centerX, centerY);
    ctx.stroke();
  }

  [currentZOffset, zVelocity] = SineApproach(
    currentZOffset,
    zVelocity,
    period,
    targetZOffset,
    delta_t,
  );

  // Clamp to prevent NaN and negative values
  if (isNaN(currentZOffset) || !isFinite(currentZOffset)) {
    currentZOffset = targetZOffset;
    zVelocity = 0;
  }
  if (isNaN(zVelocity) || !isFinite(zVelocity)) {
    zVelocity = 0;
  }

  // Collect text labels to draw later (after all ellipses and hold segments)
  const textLabels = [];

  // Draw finger actions (ovals and holds)
  for (let eventIdx = 0; eventIdx < plan.length; eventIdx++) {
    const event = plan[eventIdx];

    // Calculate 3D world Z position (distance from camera) with animated offset
    // Use absolute position since currentZOffset is absolute
    const worldZ = nearZ + eventIdx * worldSpacePerEvent - currentZOffset;

    // Perspective projection: screenY = vanishingPoint + (screenBottom - vanishingPoint) / worldZ
    // This gives proper perspective where objects at same world distance appear closer on screen as Z increases
    const eventY = vanishingPoint + (screenBottom - vanishingPoint) / worldZ;

    // Don't skip off-screen events - let browser handle clipping

    // Calculate perspective factor for sizing (0 at vanishing point, 1 at near plane)
    const perspectiveFactor = 1 / worldZ;

    // Draw for each finger
    for (let fingerIdx = 0; fingerIdx < numFingers; fingerIdx++) {
      const action = event[fingerIdx];
      if (!action || action === "") continue;

      // Calculate X position with perspective
      const fingerX =
        centerX + (fingerBottomX[fingerIdx] - centerX) * perspectiveFactor;

      // Calculate size with perspective
      // Width should match the spacing between finger lines at this depth
      const laneWidth = fingerSpacing * perspectiveFactor;
      const ovalWidth = laneWidth * 0.35; // Use proportion of lane width

      // Height: calculate by moving forward/backward along the guide line
      const ovalZRadius = 0.06; // Radius in Z direction
      const frontZ = worldZ - ovalZRadius;
      const backZ = worldZ + ovalZRadius;

      const frontY = vanishingPoint + (screenBottom - vanishingPoint) / frontZ;
      const backY = vanishingPoint + (screenBottom - vanishingPoint) / backZ;

      // Check if ellipse should be drawn (top edge not below screen bottom)
      const shouldDrawEllipse = worldZ > 0.5;

      if (eventIdx % 2 === 0) {
        // Even index: PRESS action
        if (action === "1" || action === "2" || action === "3") {
          // Check if this is an initial press (not preceded by hold)
          const isInitialPress =
            eventIdx === 0 ||
            (eventIdx > 0 && plan[eventIdx - 1][fingerIdx] !== "hold");

          if (isInitialPress && shouldDrawEllipse) {
            const ovalHeight = Math.abs(eventY - backY);
            // Draw filled oval for initial button press
            const keyColor = getKeyColor(action);
            const alpha = Math.round(0.8 * perspectiveFactor * 255)
              .toString(16)
              .padStart(2, "0");
            ctx.fillStyle = `${keyColor}${alpha}`;
            ctx.beginPath();
            ctx.ellipse(
              fingerX,
              eventY,
              ovalWidth,
              ovalHeight,
              0,
              0,
              Math.PI * 2,
            );
            ctx.fill();

            // Store text label info to draw later
            textLabels.push({
              action,
              fingerX,
              eventY,
              perspectiveFactor,
            });
          }

          // Draw continuous hold segment from initial press to release
          // Only draw if this is the initial press
          if (isInitialPress) {
            // Find the release event for this press
            let releaseIdx = eventIdx + 1;
            while (
              releaseIdx < plan.length &&
              plan[releaseIdx][fingerIdx] === "hold"
            ) {
              releaseIdx += 2; // Skip to next release event (odd indices)
            }

            // Draw continuous line from press to release
            if (
              releaseIdx < plan.length &&
              plan[releaseIdx][fingerIdx] === "release"
            ) {
              // Calculate release position using absolute positioning
              const releaseWorldZ =
                nearZ + releaseIdx * worldSpacePerEvent - currentZOffset;
              if (releaseWorldZ > 0.5) {
                // Calculate line start/end positions by offsetting Z slightly
                // Start just after the press event, clipped at 0.5 to prevent going behind screen
                const startZ = Math.max(worldZ + 0.05, 0.5);
                const startPerspectiveFactor = 1 / startZ;
                const startY =
                  vanishingPoint + (screenBottom - vanishingPoint) / startZ;
                const startX =
                  centerX +
                  (fingerBottomX[fingerIdx] - centerX) * startPerspectiveFactor;

                // End just before the release event
                const endZ = releaseWorldZ - 0.05;
                const endPerspectiveFactor = 1 / endZ;
                const endY =
                  vanishingPoint + (screenBottom - vanishingPoint) / endZ;
                const endX =
                  centerX +
                  (fingerBottomX[fingerIdx] - centerX) * endPerspectiveFactor;

                // Draw as a filled quadrilateral with perspective-correct width
                const startHalfWidth = 25 * startPerspectiveFactor;
                const endHalfWidth = 25 * endPerspectiveFactor;

                // Calculate the four corners
                // Direction perpendicular to the guide line (horizontal in screen space)
                const startLeftX = startX - startHalfWidth;
                const startRightX = startX + startHalfWidth;
                const endLeftX = endX - endHalfWidth;
                const endRightX = endX + endHalfWidth;

                // Select color based on key number
                const holdColor = getKeyColor(action);
                const holdAlpha = Math.round(0.8 * 255 * endPerspectiveFactor)
                  .toString(16)
                  .padStart(2, "0");
                ctx.fillStyle = `${holdColor}${holdAlpha}`;
                ctx.beginPath();
                ctx.moveTo(startLeftX, startY);
                ctx.lineTo(endLeftX, endY);
                ctx.lineTo(endRightX, endY);
                ctx.lineTo(startRightX, startY);
                ctx.closePath();
                ctx.fill();
              }
            }
          }
        }
      } else {
        // Odd index: RELEASE action
        if (action === "release" && shouldDrawEllipse) {
          // Find the corresponding press action to determine color
          // Look backwards to find the most recent press on this finger
          let pressAction = "1"; // Default to key 1
          for (let prevIdx = eventIdx - 1; prevIdx >= 0; prevIdx--) {
            const prevEvent = plan[prevIdx];
            const prevFingerAction = prevEvent[fingerIdx];
            if (
              prevFingerAction === "1" ||
              prevFingerAction === "2" ||
              prevFingerAction === "3"
            ) {
              pressAction = prevFingerAction;
              break;
            }
          }

          const ovalHeight = Math.abs(eventY - backY);
          // Draw empty oval for release with color matching the press
          const releaseColor = getKeyColor(pressAction);
          const releaseAlpha = Math.round(0.8 * perspectiveFactor * 255)
            .toString(16)
            .padStart(2, "0");
          ctx.strokeStyle = `${releaseColor}${releaseAlpha}`;
          ctx.lineWidth = 4 * perspectiveFactor;
          ctx.beginPath();
          ctx.ellipse(
            fingerX,
            eventY,
            ovalWidth,
            ovalHeight,
            0,
            0,
            Math.PI * 2,
          );
          ctx.stroke();
        }
      }
    }
  }

  // Draw all text labels on top of everything, from back to front
  // Sort by perspectiveFactor (smaller = farther away)
  textLabels.sort((a, b) => a.perspectiveFactor - b.perspectiveFactor);

  for (const label of textLabels) {
    const { action, fingerX, eventY, perspectiveFactor } = label;

    const laneWidth = fingerSpacing * perspectiveFactor;
    const ovalWidth = laneWidth * 0.35;

    renderAction(ctx, fingerX, eventY, action, { height: ovalWidth * 0.9 });
  }

  ctx.textAlign = "left";
  ctx.textBaseline = "alphabetic";
}

const barWidth = 15; // Increased width for the background bars

function renderWPMBar(ctx, height, wpm) {
  const barX = 0; // Touch left edge
  const barBottomY = height; // Touch bottom edge
  const barHeight = height;

  // Scale so that targetWPM appears at exactly center (height/2)
  // This means targetWPM maps to height/2, so maxWPM = targetWPM * 2
  const maxWPM = targetWPM * 2;

  {
    let ones = wpm % 10;
    let tens = Math.floor(wpm / 10) % 10;
    if (ones > 9) {
      tens += ones - 9;
    }
    let hundreds = Math.floor(wpm / 100) % 10;
    if (tens > 9) {
      hundreds += tens - 9;
    }

    ctx.fillStyle = "#5D6532";
    ctx.beginPath();
    ctx.roundRect(-10, barBottomY - 60, 95, 80, 10);
    ctx.filter = "drop-shadow(0px 0px 8px rgba(0, 0, 0, 0.5))";
    ctx.fill();
    ctx.filter = "none";

    drawBarrel(ctx, barX + barWidth + 5, barBottomY - 30, 20, 25, hundreds);
    drawBarrel(ctx, barX + barWidth + 24, barBottomY - 30, 20, 25, tens);
    drawBarrel(ctx, barX + barWidth + 43, barBottomY - 30, 20, 25, ones);

    ctx.fillStyle = "#d4d4d4";
    ctx.font = "22px 'Bunker Stencil', monospace";
    ctx.textAlign = "right";
    ctx.fillText("WPM", barX + barWidth + 62, barBottomY - 35);
    ctx.textAlign = "left";
  }

  ctx.fillStyle = "#111";
  ctx.filter = "drop-shadow(0px 0px 5px #000000)";
  ctx.fillRect(barX, 0, barWidth, barHeight);
  ctx.filter = "none";

  // Calculate positions for green/red section markers
  const targetY = height / 2; // Target WPM at center

  // Draw red section marker (below target) along outer edge
  ctx.fillStyle = "#660000"; // Dark red, not pulling attention
  ctx.fillRect(barX, targetY, barWidth / 3, barBottomY - targetY);

  // Draw green section marker (above target) along outer edge
  ctx.fillStyle = "#006600"; // Dark green, not pulling attention
  ctx.fillRect(barX, 0, barWidth / 3, targetY);

  // Draw ticks every 1 WPM
  ctx.fillStyle = "#aaa"; // Dark gray ticks
  for (let tickWPM = 0; tickWPM <= maxWPM; tickWPM += 1) {
    const tickY = barBottomY - (tickWPM / maxWPM) * barHeight;
    const tickWidth = tickWPM % 5 === 0 ? barWidth : (barWidth * 2) / 3; // Longer ticks every 5 WPM
    ctx.fillRect(barX, tickY - 0.5, tickWidth, 1);
  }

  // Draw target WPM tick
  ctx.fillStyle = "#ccc";
  ctx.fillRect(barX, targetY - 1.5, barWidth, 3);

  // Draw target WPM number next to the tick
  ctx.fillStyle = "#aaa";
  ctx.font = "12px 'Modern Typewriter', monospace";
  ctx.textAlign = "left";
  ctx.fillText(targetWPM.toString(), barX + barWidth + 5, targetY + 4);

  // Draw white dial indicator for current WPM along the edge
  const currentHeight = Math.min((wpm / maxWPM) * barHeight, barHeight);
  const currentY = barBottomY - currentHeight;

  // Draw dial as a triangle pointing right
  ctx.fillStyle = "#ffffff";
  ctx.beginPath();
  ctx.moveTo(barX, currentY); // Point at edge
  ctx.lineTo(barX + 8, currentY - 5); // Top corner
  ctx.lineTo(barX + barWidth, currentY - 5); // Top corner
  ctx.lineTo(barX + barWidth, currentY + 5); // Bottom corner
  ctx.lineTo(barX + 8, currentY + 5); // Bottom corner
  ctx.closePath();
  ctx.fill();
}

function renderAccuracyBar(ctx, width, height, accuracy) {
  const barX = width - barWidth; // Touch right edge
  const barBottomY = height; // Touch bottom edge
  const perfectHeight = height * 0.5;
  const barHeight = perfectHeight / targetAccuracy;
  const barY = barBottomY - barHeight;

  const maxAccuracy = 100;

  const targetY = height / 2; // Target accuracy at center

  {
    // Draw accuracy text at bottom right

    let accOnes = accuracy % 10;
    let accTens = Math.floor(accuracy / 10) % 10;
    if (accOnes > 9) {
      accTens += accOnes - 9;
    }
    let accHundreds = Math.floor(accuracy / 100) % 10;
    if (accTens > 9) {
      accHundreds += accTens - 9;
    }

    ctx.fillStyle = "#5D6532";
    ctx.beginPath();
    ctx.roundRect(width + 10, barBottomY - 60, -95, 80, 10);
    ctx.filter = "drop-shadow(0px 0px 8px rgba(0, 0, 0, 0.5))";
    ctx.fill();
    ctx.filter = "none";

    drawBarrel(ctx, barX - 63, barBottomY - 30, 20, 25, accHundreds);
    drawBarrel(ctx, barX - 44, barBottomY - 30, 20, 25, accTens);
    drawBarrel(ctx, barX - 25, barBottomY - 30, 20, 25, accOnes);

    ctx.textAlign = "right";
    ctx.fillStyle = "#d4d4d4";
    ctx.save();
    ctx.translate(barX - 8, barBottomY - 35);
    ctx.scale(1.3, 1);
    ctx.font = "22px 'Bunker Stencil', monospace";
    ctx.fillText("ACC", 0, 0);
    ctx.restore();

    ctx.textAlign = "left";
  }

  // Draw black background bar (full height, skeuomorphic - not 100% black)
  ctx.fillStyle = "#111";
  ctx.filter = "drop-shadow(0 0 5px black)";
  ctx.fillRect(barX, barY, barWidth, barHeight);
  ctx.filter = "none";

  // Draw red section marker (below target) along outer edge
  ctx.fillStyle = "#660000"; // Dark red, not pulling attention
  ctx.fillRect(width, targetY, -barWidth / 3, barBottomY - targetY);

  // Draw green section marker (above target) along outer edge
  ctx.fillStyle = "#006600"; // Dark green, not pulling attention
  ctx.fillRect(width, barY, -barWidth / 3, targetY - barY);

  // Draw ticks every 1% (smaller) and every 10% (larger)
  ctx.fillStyle = "#aaa"; // Dark gray ticks
  for (let tickPercent = 0; tickPercent <= maxAccuracy; tickPercent += 5) {
    const tickY = barBottomY - (tickPercent / maxAccuracy) * barHeight;
    const tickWidth = tickPercent % 10 === 0 ? barWidth : (barWidth * 2) / 3;
    ctx.fillRect(width, tickY - 0.5, -tickWidth, 1);
  }

  // Draw target accuracy tick at exactly center right
  ctx.fillStyle = "#ccc";
  ctx.fillRect(width, targetY - 1.5, -barWidth, 3);

  // Draw target accuracy number next to the tick
  ctx.fillStyle = "#858585";
  ctx.font = "12px 'Modern Typewriter', monospace";
  ctx.textAlign = "right";
  ctx.fillText(
    Math.round(targetAccuracy * 100).toString() + "%",
    barX - 5,
    targetY + 4,
  );

  // Draw white dial indicator for current accuracy along the edge
  const currentHeight = Math.min(
    (accuracy / maxAccuracy) * barHeight,
    barHeight,
  );
  const currentY = barBottomY - currentHeight;

  // Draw dial as a triangle pointing left
  ctx.fillStyle = "#ffffff";
  ctx.beginPath();
  ctx.moveTo(barX + barWidth, currentY);
  ctx.lineTo(barX + barWidth - 8, currentY - 5);
  ctx.lineTo(barX - 3, currentY - 5);
  ctx.lineTo(barX - 3, currentY + 5);
  ctx.lineTo(barX + barWidth - 8, currentY + 5);
  ctx.closePath();
  ctx.fill();
}

function renderDebugLogs(ctx, width, height) {
  if (typeof debugLogs === "undefined" || debugLogs.length === 0) return;

  const fontSize = 14;
  const lineHeight = 18;
  const padding = 10;
  const rightMargin = 10;
  const topMargin = 10;

  ctx.font = `${fontSize}px 'Modern Typewriter', monospace`;
  ctx.textAlign = "right";

  // Render each log message from top to bottom
  for (let i = 0; i < debugLogs.length; i++) {
    const y = topMargin + (i + 1) * lineHeight;
    const x = width - rightMargin;

    // Draw semi-transparent background for readability
    ctx.fillStyle = "rgba(0, 0, 0, 0.7)";
    const textWidth = ctx.measureText(debugLogs[i]).width;
    ctx.fillRect(
      x - textWidth - padding,
      y - fontSize,
      textWidth + padding * 2,
      lineHeight,
    );

    // Draw log text
    ctx.fillStyle = "#4ec9b0";
    ctx.fillText(debugLogs[i], x, y);
  }

  ctx.textAlign = "left";
}
