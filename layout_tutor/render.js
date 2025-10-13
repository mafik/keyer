// Rendering function for the tutor interface
// Uses global state variables from state.js and tutor.js

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

  // Render main text area
  renderTextArea(ctx, width, height);

  // Animate tachometer using SineApproach
  const targetLevel = currentLevel();
  const tachometerPeriod = 3.0; // 3 second animation period
  const delta_t = 1 / 60; // Fixed 60 FPS timestep

  [tachometerLevel, tachometerVelocity] = SineApproach(
    tachometerLevel,
    tachometerVelocity,
    tachometerPeriod,
    targetLevel,
    delta_t,
  );

  // Render fingerplan
  renderFingerplan(ctx, width, height);

  // Render stats
  renderStats(ctx, width, height);

  // Render debug logs
  // disabled
  // renderDebugLogs(ctx, width, height);

  // Decide whether to continue animation
  // Animation continues if:
  // 1. Less than 5 seconds since last char, OR
  // 2. Z velocity is above threshold (animation still in progress), OR
  // 3. Tachometer velocity is above threshold (tachometer animating)
  const velocityThreshold = 0.01;
  let shouldContinueAnimation =
    Math.abs(zVelocity) >= velocityThreshold ||
    Math.abs(tachometerVelocity) >= velocityThreshold;

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
    let color;
    if (i === newIndex) {
      color = "#4ec9b0"; // Cyan for NEW
    } else if (i === oldIndex && oldIndex !== newIndex) {
      color = "#ce9178"; // Orange for OLD
    } else if (i < newIndex) {
      color = "#6a9955"; // Green for learned
    } else {
      color = "#3e3e42"; // Dark gray for not yet learned
    }

    // chord format starts with Thumb: chord[0]=Thumb, chord[1]=Index, etc.
    // Display order: Thumb at top (row 0), Little at bottom (row 4)
    // So they map directly: display row = chord index

    for (let finger = 0; finger < 5; finger++) {
      const y = gridStartY + finger * lineSpacing;
      const position = chord[finger];

      if (position !== "0") {
        // Draw bright circle
        ctx.fillStyle = color;
        ctx.beginPath();
        ctx.arc(x, y, circleRadius, 0, Math.PI * 2);
        ctx.fill();

        // Draw position number inside
        ctx.fillStyle = "#1e1e1e";
        ctx.font = "bold 12px 'Modern Typewriter', monospace";
        ctx.fillText(position, x, y + 4);
      }
    }
  }

  ctx.textAlign = "left";
}

function renderTextArea(ctx, width, height) {
  const baseFontSize = 48;
  const maxWidth = width - 120; // Leave margin on both sides

  // Set initial font to measure text
  ctx.font = `${baseFontSize}px 'Modern Typewriter', monospace`;

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
  ctx.font = `${fontSize}px 'Modern Typewriter', monospace`;

  // Measure again with scaled font to get accurate character width
  const charWidth = ctx.measureText("A").width;

  const startY = height / 2 + 24 * scale;

  // Calculate text positioning
  const totalWidth = targetText.length * charWidth;
  const startX = width / 2 - totalWidth / 2;

  // Render each character
  for (let i = 0; i < targetText.length; i++) {
    const char = targetText[i];
    const x = startX + i * charWidth;

    if (i < typedText.length) {
      // Already typed
      if (typedText[i] === char) {
        ctx.fillStyle = "#6a9955"; // Correct - green
      } else {
        ctx.fillStyle = "#f44747"; // Incorrect - red
      }
    } else if (i === typedText.length || i === typedText.length + 1) {
      // Current character and next character to type
      ctx.fillStyle = "#4ec9b0"; // Cyan highlight
      // Draw cursor only for the current character (first of the two)
      if (i === typedText.length) {
        const cursorHeight = 55 * scale;
        const cursorWidth = 4 * scale;
        const cursorOffset = 40 * scale;
        ctx.fillRect(
          x - cursorWidth / 2,
          startY - cursorOffset,
          cursorWidth,
          cursorHeight,
        );
      }
    } else {
      // Not yet typed
      ctx.fillStyle = "#d4d4d4"; // Default grey
    }

    ctx.fillText(char === " " ? "␣" : char, x, startY);
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

      // Calculate top edge of oval using Z information (back of oval is the top edge)
      const topEdgeY = backY;

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
            // Sandy desert color: cooler tan/beige with less red
            ctx.fillStyle = `rgba(194, 178, 128, ${0.8 * perspectiveFactor})`;
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
                const startHalfWidth = 15 * startPerspectiveFactor;
                const endHalfWidth = 15 * endPerspectiveFactor;

                // Calculate the four corners
                // Direction perpendicular to the guide line (horizontal in screen space)
                const startLeftX = startX - startHalfWidth;
                const startRightX = startX + startHalfWidth;
                const endLeftX = endX - endHalfWidth;
                const endRightX = endX + endHalfWidth;

                // Sandy desert color for highlight segments
                ctx.fillStyle = `rgba(194, 178, 128, 0.6)`;
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
          const ovalHeight = Math.abs(eventY - backY);
          // Draw empty oval for release with sandy desert color
          ctx.strokeStyle = `rgba(194, 178, 128, ${0.8 * perspectiveFactor})`;
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

    // Draw button number with Bunker Stencil font
    const fontSize = 43.2 * perspectiveFactor;
    ctx.font = `${fontSize}px 'Bunker Stencil', monospace`;
    ctx.textAlign = "center";
    ctx.textBaseline = "bottom";

    ctx.strokeStyle = `#353022`;
    ctx.lineWidth = 10 * perspectiveFactor;
    ctx.strokeText(`${action}`, fingerX, eventY);

    // Draw white fill
    ctx.fillStyle = "rgba(255, 255, 255, 1)";
    ctx.fillText(`${action}`, fingerX, eventY);
  }

  ctx.textAlign = "left";
  ctx.textBaseline = "alphabetic";
}

function renderStats(ctx, width, height) {
  const wpm = getCurrentWPM();
  const accuracy = getCurrentAccuracy();

  // Render vertical WPM bar on left
  renderWPMBar(ctx, height, wpm);

  // Render vertical accuracy bar on right
  renderAccuracyBar(ctx, width, height, accuracy);

  ctx.textAlign = "left";
}

const barWidth = 6;

function renderWPMBar(ctx, height, wpm) {
  const barX = 0; // Touch left edge
  const barBottomY = height; // Touch bottom edge
  const barHeight = height;

  // Scale so that targetWPM appears at exactly center (height/2)
  // This means targetWPM maps to height/2, so maxWPM = targetWPM * 2
  const maxWPM = targetWPM * 2;

  // Draw target WPM tick at exactly center left
  const targetY = height / 2; // Exactly center vertically
  ctx.fillStyle = "#858585";
  ctx.fillRect(barX, targetY - 1, barWidth + 10, 2);

  // Draw target WPM number next to the tick
  ctx.fillStyle = "#858585";
  ctx.font = "16px 'Modern Typewriter', monospace";
  ctx.textAlign = "left";
  ctx.fillText(targetWPM.toString(), barX + barWidth + 10, targetY + 5);

  // Draw current WPM bar
  const currentHeight = Math.min((wpm / maxWPM) * barHeight, barHeight);
  const currentY = barBottomY - currentHeight;
  ctx.fillStyle = "#4ec9b0";
  ctx.fillRect(barX, currentY, barWidth, currentHeight);

  // Draw WPM text at bottom left
  ctx.fillStyle = "#4ec9b0";
  ctx.font = "20px 'Modern Typewriter', monospace";
  ctx.textAlign = "left";
  ctx.fillText(
    Math.round(wpm).toString(),
    barX + barWidth + 10,
    barBottomY - 30,
  );

  // Draw "WPM" label
  ctx.fillStyle = "#858585";
  ctx.font = "12px 'Modern Typewriter', monospace";
  ctx.fillText("WPM", barX + barWidth + 10, barBottomY - 15);

  ctx.textAlign = "left";
}

function renderAccuracyBar(ctx, width, height, accuracy) {
  const barX = width - barWidth; // Touch right edge
  const barBottomY = height; // Touch bottom edge
  const barHeight = height;

  // Scale so that targetAccuracy appears at exactly center (height/2)
  // This means targetAccuracy (90%) maps to height/2, so max = targetAccuracy * 2
  const maxAccuracy = targetAccuracy * 100 * 2; // Convert to percentage and double

  // Draw target accuracy tick at exactly center right
  const targetY = height / 2; // Exactly center vertically
  ctx.fillStyle = "#858585";
  ctx.fillRect(barX - 10, targetY - 1, barWidth + 10, 2);

  // Draw 100% accuracy tick
  const perfectHeight = (100 / maxAccuracy) * barHeight;
  const perfectY = barBottomY - perfectHeight;
  ctx.fillStyle = "#6a9955"; // Green for 100%
  ctx.fillRect(barX - 10, perfectY - 1, barWidth + 10, 2);

  // Draw current accuracy bar
  const currentHeight = Math.min(
    (accuracy / maxAccuracy) * barHeight,
    barHeight,
  );
  const currentY = barBottomY - currentHeight;
  ctx.fillStyle = "#4ec9b0";
  ctx.fillRect(barX, currentY, barWidth, currentHeight);

  // Draw accuracy text at bottom right
  ctx.fillStyle = "#4ec9b0";
  ctx.font = "20px 'Modern Typewriter', monospace";
  ctx.textAlign = "right";
  ctx.fillText(accuracy.toString() + "%", barX - 10, barBottomY - 30);

  // Draw "ACC" label
  ctx.fillStyle = "#858585";
  ctx.font = "12px 'Modern Typewriter', monospace";
  ctx.fillText("ACC", barX - 10, barBottomY - 15);

  ctx.textAlign = "left";
}

function renderStat(ctx, x, y, label, value) {
  ctx.fillStyle = "#858585";
  ctx.font = "14px 'Modern Typewriter', monospace";
  ctx.fillText(label, x, y - 20);

  ctx.fillStyle = "#4ec9b0";
  ctx.font = "24px 'Modern Typewriter', monospace";
  ctx.fillText(value, x, y);
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
