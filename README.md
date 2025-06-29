# 🥔 Batata — A Lightning-Fast Terminal Text Editor in C

**Batata** is a minimal, ultra-fast text editor written in C, inspired by [kilo](https://github.com/antirez/kilo). It lets you edit files directly from the terminal with high performance and low memory usage.

🛠️ **Linux-only** • 🚀 **Superfast** • ✨ **Minimalist**

> ⚠️ **Work In Progress (WIP)**: Vim motions and multi-level undo are on the way. Contributions are welcome!

---

## ✨ Features

* ⚡ **Instant startup** — no loading screens, no lag
* 🧼 **Clean codebase** — just C and standard libraries
* 📝 **Edit any file** — plain text, config, code, you name it
* 💻 **Terminal-native** — runs inside your favorite shell
* 📟 **Status bar** for context awareness

---

## 🛣️ Roadmap / Coming Soon

* 🧭 **Vim-style motions** (`w`, `b`, `gg`, `G`, etc.)
* 🔁 **Multi-level undo/redo** system
* 🔍 **Improved search & highlight**
* ⚙️ **Custom keybindings** and settings file

---

## 🧰 Setup Instructions

### ✅ Requirements

* A Linux system
* GCC or any C compiler
* `make` utility

### 🏗️ Build & Run

1. **Clone the repository**:

   ```bash
   git clone https://github.com/Rational-Idiot/batata.git
   cd batata
   ```

2. **Build the project**:

   ```bash
   make
   ```

3. **Open a file with Batata**:

   ```bash
   ./batata yourfile.txt
   ```

---

## 📁 File Structure

```text
batata/
├── batata.c       # Main source code
├── Makefile       # Build instructions
└── README.md      # Project documentation
```

---

## 📄 License

Batata is licensed under the **MIT License**. See the `LICENSE` file for details.

---

## 🤔 Why the name *"Batata"*?

Because it's small, simple, and dependable — just like a potato. And let’s be honest: who doesn’t love potatoes?

---

## 💬 Contributing

Feel free to fork, open issues, and submit pull requests. Let’s build a lightning-fast terminal editor together.

---

🧠 **Pro tip:** Alias it for quick access:

```bash
alias bt='./batata'
```

---

Happy editing! 🚀
