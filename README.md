# status

Minimal status bar for X. Works by writing a string to the root window name
which is subsequently displayed by the window manager (e.g.
[`dwm`](https://dwm.suckless.org/)).

## Building

Run the following commands:

```bash
$ git clone https://github.com/niculaionut/status.git
$ cd status
$ sh ./compile.sh
$ sudo sh ./install.sh # optional
```

## Usage

Run `statusd` in the background. Issue updates with the command `status <id-of-field>...`.


### Adding fields

See [`add_field.diff`](add_field.diff) (and optionally run `git apply add_field.diff`), which adds a system memory field having index 3.

### Example

```bash
$ cd /path/to/status
$ ./statusd # Run in the background
```

```bash
$ cd /path/to/status
$ ./status 0 # Update the Time field
$ ./status 1 # Update the System Load field
$ ./status 2 # Update the CPU Temperature field
$ ./status 0 1 2 # Update all three fields
```
