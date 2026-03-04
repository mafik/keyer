// Word picker logic for selecting practice words with known bigrams

let n = learning_sequence.length;

let charToIndex = {};
for (let i = 0; i < n; i++) {
  charToIndex[learning_sequence[i]] = i;
}

function calcLevel(i1, i2) {
  const max = Math.max(i1, i2);
  const min = Math.min(i1, i2);
  if (i1 == i2) {
    return -1;
  }
  return (max * (max - 1)) / 2 + min;
}

function currentLevel() {
  return calcLevel(Math.max(oldIndex, 0), newIndex);
}

function maxLevel() {
  return calcLevel(n - 2, n - 1);
}

let studyLists = [];
for (let i = 0; i <= maxLevel(); i++) {
  studyLists[i] = [];
}

// Save display words before the loop overwrites values with levels
let displayWords = {};
for (let word in wordsDictionary) {
  let val = wordsDictionary[word];
  displayWords[word] = (typeof val === "string") ? val : word;
}

for (let word in wordsDictionary) {
  let wordLevel = -1;
  for (let i = 1; i < word.length; i++) {
    let charIndexI = charToIndex[word[i]];
    let charIndexJ = charToIndex[word[i - 1]];
    let bigramLevel = calcLevel(charIndexI, charIndexJ);
    wordLevel = Math.max(bigramLevel, wordLevel);
  }
  if (Number.isNaN(wordLevel) || wordLevel == -1) {
    wordLevel = 999;
  } else {
    studyLists[wordLevel].push(word);
  }
  wordsDictionary[word] = wordLevel;
}

// Pick words that only include known bigrams and at least one NEW/OLD or OLD/NEW transition
function pickWordsForPractice(count = 3) {
  let ret = [];
  let wordList = studyLists[currentLevel()];

  if (wordList.length < 2) {
    const a = learning_sequence[oldIndex];
    const b = learning_sequence[newIndex];
    // Pick a random previously-learned letter (excluding a and b), or nothing
    let candidates = [];
    for (let i = 0; i <= oldIndex; i++) {
      candidates.push(learning_sequence[i]);
    }
    // Add "nothing" as an option
    candidates.push("");
    for (let i = 0; i < count; i++) {
      const c = candidates[Math.floor(Math.random() * candidates.length)];
      ret.push(Math.random() < 0.5 ? a + b + c : c + b + a);
    }
    return ret;
  }

  for (let i = 0; i < count; i++) {
    let word = wordList[Math.floor(Math.random() * wordList.length)];
    ret.push(word);
  }

  return ret;
}
