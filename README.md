# 🥔 Batata Editor

A lightweight, Vi-inspired terminal text editor written in C (~3300 lines). Batata combines the power of modal editing with modern features like syntax highlighting, mouse support, and intuitive key bindings.

## Features

### Core Editing
- **Modal editing** - Normal, Insert, Visual, and Replace modes
- **Vi-like key bindings** - Familiar navigation and editing commands
- **Undo/Redo system** - Full edit history with `u` and `Ctrl+R`
- **Cut, Copy, Paste** - Text manipulation with clipboard support
- **Find functionality** - Search through files with `/`

### Advanced Navigation
- **Motion commands** - `h/j/k/l`, `w/b/e`, `f/t/F/T` for precise cursor movement
- **Numeric prefixes** - Repeat commands with number prefixes (e.g., `5j`)
- **Line jumping** - `gg`, `G`, `H/M/L` for quick navigation
- **Bracket matching** - `%` to jump between matching brackets
- **Scrolling** - Page up/down, half-page scrolling with `Ctrl+U/D`

### Visual Enhancements
- **Syntax highlighting** - Color-coded syntax for better readability
- **Line numbers** - Absolute or relative line numbering
- **Mouse support** - Click to position cursor, scroll with mouse wheel
- **Visual selection** - Select text blocks in visual mode
- **Status messages** - Informative status bar with tips and warnings

### Editor Modes
- **Normal mode** (`n`) - Navigation and commands
- **Insert mode** (`i`) - Text insertion
- **Visual mode** (`v`) - Text selection
- **Replace mode** (`r`) - Character replacement
- **Dumb mode** (`-d`) - Simplified mode for basic terminals

## Installation

### Prerequisites
- POSIX-compliant terminal
- Linux/Unix environment

<details>
<summary><b>📦 Ubuntu/Debian (via PPA) - Recommended</b></summary>

```bash
# Add the PPA repository
sudo add-apt-repository ppa:rational-idiot/ppa

# Update package list and install
sudo apt update && sudo apt install batata
```

This is the easiest method for Ubuntu and Debian-based distributions.
- ⚠️ Configuration Note for v1.0.0:
The current Ubuntu release (v1.0.0) has a bug where it looks for the configuration file .batatarc in the source directory where batata.c is located, not in the standard configuration directory. This means configuration is not available for PPA installations.
Workaround for v1.0.0 PPA users:
Unfortunately, there's no practical way to create a configuration file for the PPA installation due to this bug. You'll need to wait for a future release or build from source if you need configuration options.

</details>

<details>
<summary><b>📦 Debian-based Distributions (.deb package)</b></summary>

```bash
# Download the .deb package from the latest release
wget https://github.com/Rational-Idiot/batata/releases/download/v1.0.0/batata.deb 
# Install the package
sudo dpkg -i batata.deb

# If there are dependency issues, fix them with:
sudo apt-get install -f
```

</details>

<details>
<summary><b>📦 Red Hat/Fedora/CentOS (.rpm package)</b></summary>

```bash
# Download the .rpm package from the latest release
wget https://github.com/Rational-Idiot/batata/releases/download/v1.0.0/batata-1.0-1.el9.x86_64.rpm

# Install the package
sudo rpm -ivh batata-1.0.0-1.x86_64.rpm

# Or using dnf (Fedora)
sudo dnf install batata-1.0.0-1.x86_64.rpm

# Or using yum (CentOS/RHEL)
sudo yum install batata-1.0.0-1.x86_64.rpm
```

</details>

<details>
<summary><b>🔧 Build from Source</b></summary>

**Prerequisites:**
- GCC compiler
- Make

```bash
# Clone the repository
git clone https://github.com/Rational-Idiot/batata
cd batata

# Compile the editor
make
```

**Adding to PATH for different shells:**

<details>
<summary>Bash</summary>

```bash
# Add to ~/.bashrc
echo 'export PATH="$PATH:/path/to/batata"' >> ~/.bashrc
source ~/.bashrc
```

</details>

<details>
<summary>Zsh</summary>

```bash
# Add to ~/.zshrc
echo 'export PATH="$PATH:/path/to/batata"' >> ~/.zshrc
source ~/.zshrc
```

</details>

<details>
<summary>Fish</summary>

```bash
# Add to fish config
echo 'set -gx PATH $PATH /path/to/batata' >> ~/.config/fish/config.fish
source ~/.config/fish/config.fish
```

</details>

**Or create a symbolic link:**
```bash
# Create a symlink in a directory that's already in PATH
sudo ln -s /path/to/batata/batata /usr/local/bin/batata
```

</details>

### Configuration
Batata looks for configuration in `~/.config/batata/.batatarc`. Create this directory and file to customize your editor settings.
The default config is
```
TAB_LENGTH=2                     // Set how many spaces a TAB insertion is rendered as
RELATIVE_LINE_NUMBERS=1          // Set to 0 to use absolute line numbers
UNDO_STACK_SIZE=100              // Sets how much memory the Undo Stack consumes
AUTO_COMPLETION=1                // COmpletes (,{,<,",'
DUMB =0;                         // Only allow insert mode
```

## Usage

### Basic Usage
```bash
# Start with a new file
batata

# Open an existing file
batata filename.txt

# Start in dumb mode (insert-only)
batata -d filename.txt
```

### Key Bindings

#### All Modes
| Key | Action |
|-----|--------|
| `Ctrl+q` | Exit the editor |
| `Ctrl+s` | Save the file |

#### Normal Mode
| Key | Action |
|-----|--------|
| `i` | Enter insert mode |
| `a` | Insert after cursor |
| `o` | Open new line below |
| `O` | Open new line above |
| `s` | Delete current character and enter Insert mode |
| `gg` | Go to top of the file | 
| `G` | Go to bottom of the file |
| `v` | Enter visual mode |
| `u` | Undo |
| `Ctrl+r` | Redo |
| `dd` | Delete line |
| `D` | Delete to end of line |
| `c` | Change the text {delete and enter insert mode} |
| `yy` | Yank (copy) line |
| `p` | Paste |
| `x` | Delete character |
| `r` | Replace character |
| `R` | Enter Replace mode |
| `~` | Toggle case |
| `/` | Search |
| `Ctrl+s` | Save |
| `Ctrl+q` | Quit |
| `Ctrl+a \ Ctrl+x` | Increment\Decrement the number under the curosr |
| `Ctrl+e \ Ctrl+y`| Scroll down\up|
| `Ctrl+b \ Ctrl+f`| Scroll down\up by a page | 
| `Ctrl+d \ Ctrl+u` | Sctoll down\up by half a pge |

All of the actions like d, c and y can be simply combined with any of the following motions
``` txt
For eg - d$ deletes to the end of the line
         yM copies till the middle of the page
         c4j deletes the next 4 lines and places in insert mode
```

#### Motion Commands
| Key | Action |
|-----|--------|
| `h/j/k/l` | Move left/down/up/right |
| `w/b/e` | Word forward/backward/end |
| `0` | Beginning of line |
| `$` | End of line |
| `^` | First non-whitespace character |
| `gg` | Go to first line |
| `G` | Go to last line |
| `H/M/L` | Top/middle/bottom of screen |
| `f{char}` | Find character forward |
| `F{char}` | Find character backward |
| `t{char}` | Jump till(one character behind) the character forwards|
|`T{char}` | Jump till(One character after) the character backwards |
| `%` | Jump to matching bracket |
| `^` | Jump to first non whitespace character in the line |

#### Insert Mode
| Key | Action |
|-----|--------|
| `Esc` | Return to normal mode |
| `Ctrl+c` | Copy selection |
| `Ctrl+v` | Paste |
| `Ctrl+z` | Undo |
| `Ctrl+r` | Redo |
| `Ctrl+f` | Search |

#### Visual Mode
| Key | Action |
|-----|--------|
| `Esc` | Return to normal mode |
| `y` | Yank selection |
| `d` | Delete selection |
| `c` | Change selection |

You can use all the same motion keys in visual mode to select efficiently
### Advanced Features

#### Numeric Prefixes
Most commands can be prefixed with numbers:
```
5j    # Move down 5 lines
d3w   # Delete 3 words
y2k   # Yank 3 lines (the current one and the 2 above it {pro tip: Use relative line numebers})
```

#### Mouse Support
- **Left click** - Position cursor
- **Right click** - Context menu (future feature)
- **Scroll wheel** - Scroll up/down

#### Bracket Matching
Press `%` on any bracket `()`, `{}`, `[]`, or `<>` to jump to its matching pair. The editor intelligently handles nested brackets and ignores brackets within strings and comments.

#### Inside braces and quotes
Press `di(` to delete the text inside the `()` it deletes inside the parantheses if the cursor is inside one or searches for the next set of `()` on the current line
similarly the `i` motion can be combined with y and c to alter text inside `(), <>, [], {}, '', ""`

### Syntax Highlighting
Automatic syntax highlighting is enabled by default. The editor detects file types and applies appropriate color schemes.

## Architecture

### Core Components
- **Editor State** - Global state management in `E` structure
- **Row Management** - Text buffer handling with dynamic arrays
- **Input Processing** - Modal command processing
- **Terminal Interface** - Raw terminal mode with escape sequences
- **Syntax Engine** - Tokenization and highlighting system

### File Structure
```
batata/
├── main.c              # Literally the entire project
└── Makefile           # Build configuration
```

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

### Development Guidelines
- Follow K&R C style
- Add tests for new features
- Update documentation
- Ensure compatibility with POSIX terminals

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

- Inspired by Vi/Vim text editors
- Insppired by kilo by Antire
- Built following the "Build Your Own Text Editor" tutorial
- Terminal handling based on POSIX standards

## Roadmap

- [ ] Plugin system
- [ ] Advanced Undo supporting multiple actions (vim like tree style undo)
- [ ] Split window support
- [ ] Integrated terminal
- [ ] Language server protocol support
- [ ] Improved configuration system

---

**Batata** - Because even text editors can be delicious! 🥔
