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
  return calcLevel(oldIndex, newIndex);
}

function maxLevel() {
  return calcLevel(n - 2, n - 1);
}

let studyLists = [];
for (let i = 0; i <= maxLevel(); i++) {
  studyLists[i] = [];
}

for (let word in wordsDictionary) {
  let wordLevel = 0;
  for (let i = 1; i < word.length; i++) {
    let charIndexI = charToIndex[word[i]];
    let charIndexJ = charToIndex[word[i - 1]];
    let bigramLevel = calcLevel(charIndexI, charIndexJ);
    wordLevel = Math.max(bigramLevel, wordLevel);
  }
  if (Number.isNaN(wordLevel)) {
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
    wordList.push(
      learning_sequence[newIndex] +
        learning_sequence[oldIndex] +
        learning_sequence[newIndex],
    );
    wordList.push(
      learning_sequence[oldIndex] +
        learning_sequence[newIndex] +
        learning_sequence[oldIndex],
    );
  }

  for (let i = 0; i < count; i++) {
    let word = wordList[Math.floor(Math.random() * wordList.length)];
    ret.push(word);
  }

  return ret;
}
