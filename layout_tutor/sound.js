// Sound effects module

const goodSounds = [
  "sfx_good/453204__alienxxx__ao_subtractor_kickdrum_2_2.wav",
];

const badSounds = ["sfx_bad/454645__alienxxx__ao_subtractor_snare_2_4.wav"];

const levelUpSounds = [
  "sfx_level_up/275012__alienxxx__squadron_leader_form_up.wav",
  "sfx_level_up/275011__alienxxx__bogeys_inbound.wav",
  "sfx_level_up/275010__alienxxx__hes_on_my_6.wav",
  "sfx_level_up/275009__alienxxx__im_hit_im_hit3.wav",
  "sfx_level_up/275008__alienxxx__mayday_mayday.wav",
  "sfx_level_up/274980__alienxxx__help.wav",
  "sfx_level_up/274979__alienxxx__hostiles_inbound_prepare_for_battle.wav",
  "sfx_level_up/274978__alienxxx__we_are_being_chewed_up.wav",
  "sfx_level_up/274894__alienxxx__message_received.wav",
  "sfx_level_up/274893__alienxxx__aknowledging_orders.wav",
  "sfx_level_up/274892__alienxxx__coming_around.wav",
  "sfx_level_up/274891__alienxxx__direct_hit.wav",
  "sfx_level_up/274890__alienxxx__hes_right_beneath_you.wav",
  "sfx_level_up/274889__alienxxx__hes_right_on_top_of_you.wav",
  "sfx_level_up/274887__alienxxx__explosion_over_the_radiocut.wav",
  "sfx_level_up/274886__alienxxx__explosion_over_the_radio.wav",
  "sfx_level_up/274885__alienxxx__im_hit_im_hit.wav",
  "sfx_level_up/274880__alienxxx__say_bye_bye.wav",
  "sfx_level_up/274879__alienxxx__going_down.wav",
  "sfx_level_up/274878__alienxxx__going_down2.wav",
  "sfx_level_up/274872__alienxxx__nice_shooting.wav",
  "sfx_level_up/274871__alienxxx__somebody_get_this_guy.wav",
  "sfx_level_up/274870__alienxxx__we_are_ready_to_jump_out.wav",
  "sfx_level_up/274868__alienxxx__form_up_for_jump_out.wav",
  "sfx_level_up/274869__alienxxx__come_on_is_that_all_you_got.wav",
  "sfx_level_up/274867__alienxxx__gotta_do_better_than_that.wav",
  "sfx_level_up/274866__alienxxx__hold_on_im_almost_there.wav",
  "sfx_level_up/274327__alienxxx__i_got_a_lock_on_him.wav",
  "sfx_level_up/274326__alienxxx__negative.wav",
  "sfx_level_up/274325__alienxxx__damn_it_missed.wav",
  "sfx_level_up/274324__alienxxx__direct_hit.wav",
  "sfx_level_up/274323__alienxxx__im_going_in.wav",
  "sfx_level_up/274322__alienxxx__i_copy.wav",
  "sfx_level_up/274321__alienxxx__afirmative.wav",
  "sfx_level_up/274320__alienxxx__breaking_formation.wav",
  "sfx_level_up/274319__alienxxx__breaking_to_attack.wav",
  "sfx_level_up/274318__alienxxx__copy_that_forming_up.wav",
];

// Audio context for pitch shifting (shared across all sounds)
let audioContext = null;

// Initialize audio context (lazily, on first use)
function getAudioContext() {
  if (!audioContext) {
    audioContext = new (window.AudioContext || window.webkitAudioContext)();
  }
  return audioContext;
}

// Helper function to play a random sound from an array
function playRandomSound(soundArray) {
  if (soundArray.length === 0) return;

  const randomIndex = Math.floor(Math.random() * soundArray.length);
  const soundPath = soundArray[randomIndex];

  const audio = new Audio(soundPath);
  audio.play().catch((err) => {
    console.warn("Failed to play sound:", err);
  });
}

// Helper function to play a sound with pitch shifting using Web Audio API
function playPitchedSound(soundPath, pitchShift) {
  const ctx = getAudioContext();

  fetch(soundPath)
    .then((response) => response.arrayBuffer())
    .then((arrayBuffer) => ctx.decodeAudioData(arrayBuffer))
    .then((audioBuffer) => {
      const source = ctx.createBufferSource();
      source.buffer = audioBuffer;

      // Apply pitch shift by changing playback rate
      // pitchShift is in semitones: rate = 2^(semitones/12)
      source.playbackRate.value = Math.pow(2, pitchShift / 12);

      source.connect(ctx.destination);
      source.start(0);
    })
    .catch((err) => {
      console.warn("Failed to play pitched sound:", err);
    });
}

// Play a random "good" sound (correct key press) with pitch based on WPM
function playGood(currentWPM) {
  if (goodSounds.length === 0) return;

  const randomIndex = Math.floor(Math.random() * goodSounds.length);
  const soundPath = goodSounds[randomIndex];

  // Calculate pitch shift based on WPM
  // If currentWPM is not provided, use default (no shift)
  if (currentWPM === undefined) {
    playRandomSound(goodSounds);
    return;
  }

  // Target WPM is 30 (from state.js)
  const targetWPM = 30;
  const wpmDifference = currentWPM - targetWPM;

  // Map WPM difference to semitones
  // Each 10 WPM difference = 2 semitones
  // Above target = higher pitch, below target = lower pitch
  const pitchShift = (wpmDifference / 10) * 2;

  // Clamp pitch shift to reasonable range (-12 to +12 semitones = 1 octave each way)
  const clampedPitchShift = Math.max(-12, Math.min(12, pitchShift));

  playPitchedSound(soundPath, clampedPitchShift);
}

// Play a random "bad" sound (incorrect key press)
function playBad() {
  playRandomSound(badSounds);
}

// Play a random "level up" sound (advancing to next level)
function playLevelUp() {
  playRandomSound(levelUpSounds);
}
