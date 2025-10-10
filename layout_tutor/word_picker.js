// Word picker logic for selecting practice words with known bigrams

let n = learning_sequence.length;

let charToIndex = {};
for (let i = 0; i < n; i++) {
  charToIndex[learning_sequence[i]] = i;
}

function bigramIndex(newIndex, oldIndex) {
  return oldIndex + newIndex * n;
}

let studyLists = {};
for (let i = 0; i < n; i++) {
  for (let j = 0; j < n; j++) {
    studyLists[bigramIndex(i, j)] = [];
  }
}

for (let word in wordsDictionary) {
  let wordIndex = charToIndex[word[0]];
  for (let i = 1; i < word.length; i++) {
    let charIndexI = charToIndex[word[i]];
    let charIndexJ = charToIndex[word[i - 1]];
    let oldIndex = Math.min(charIndexI, charIndexJ);
    let newIndex = Math.max(charIndexI, charIndexJ);
    let bigramIndexNormal = bigramIndex(newIndex, oldIndex);
    wordIndex = Math.max(bigramIndexNormal, wordIndex);
  }
  if (Number.isNaN(wordIndex)) {
    wordIndex = 999;
  } else {
    studyLists[wordIndex].push(word);
  }
  wordsDictionary[word] = wordIndex;
}

// Pick words that only include known bigrams and at least one NEW/OLD or OLD/NEW transition
function pickWordsForPractice(count = 3) {
  let maxIndex = bigramIndex(newIndex, oldIndex);
  let ret = [];
  let wordList = studyLists[maxIndex];

  if (wordList.length < 2) {
    wordList.push(learning_sequence[newIndex] + learning_sequence[oldIndex]);
    wordList.push(learning_sequence[oldIndex] + learning_sequence[newIndex]);
  }

  for (let i = 0; i < count; i++) {
    let word = wordList[Math.floor(Math.random() * wordList.length)];
    ret.push(word);
  }

  return ret;
}
