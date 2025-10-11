// Rendering function for the tutor interface
// Uses global state variables from state.js and tutor.js

// Canvas - private to render.js
let canvas;
let ctx;

function initRender() {
  canvas = document.getElementById("canvas");
  ctx = canvas.getContext("2d");
  resizeCanvas();

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

  // Render main text area
  renderTextArea(ctx, width, height);

  // Render stats
  renderStats(ctx, width, height);
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
    } else if (i === typedText.length) {
      // Current character to type
      ctx.fillStyle = "#4ec9b0"; // Cyan highlight
      // Draw cursor (scaled proportionally)
      const cursorHeight = 55 * scale;
      const cursorWidth = 4 * scale;
      const cursorOffset = 40 * scale;
      ctx.fillRect(
        x - cursorWidth / 2,
        startY - cursorOffset,
        cursorWidth,
        cursorHeight,
      );
    } else {
      // Not yet typed
      ctx.fillStyle = "#d4d4d4"; // Default grey
    }

    ctx.fillText(char === " " ? "␣" : char, x, startY);
  }
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

function renderWPMBar(ctx, height, wpm) {
  const barX = 0; // Touch left edge
  const barWidth = 30;
  const barBottomY = height; // Touch bottom edge
  const barHeight = height;

  // Scale so that targetWPM appears at exactly center (height/2)
  // This means targetWPM maps to height/2, so maxWPM = targetWPM * 2
  const maxWPM = targetWPM * 2;

  // Draw target WPM tick at exactly center left
  const targetY = height / 2; // Exactly center vertically
  ctx.fillStyle = "#858585";
  ctx.fillRect(barX, targetY - 1, barWidth + 10, 2);

  // Draw current WPM bar
  const currentHeight = Math.min((wpm / maxWPM) * barHeight, barHeight);
  const currentY = barBottomY - currentHeight;
  ctx.fillStyle = "#4ec9b0";
  ctx.fillRect(barX, currentY, barWidth, currentHeight);

  // Draw WPM text at bottom left
  ctx.fillStyle = "#4ec9b0";
  ctx.font = "20px 'Modern Typewriter', monospace";
  ctx.textAlign = "left";
  ctx.fillText(wpm.toString(), barX + barWidth + 10, barBottomY - 30);

  // Draw "WPM" label
  ctx.fillStyle = "#858585";
  ctx.font = "12px 'Modern Typewriter', monospace";
  ctx.fillText("WPM", barX + barWidth + 10, barBottomY - 15);

  ctx.textAlign = "left";
}

function renderAccuracyBar(ctx, width, height, accuracy) {
  const barWidth = 30;
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
