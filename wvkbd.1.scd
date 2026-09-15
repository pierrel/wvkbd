wvkbd(1)

# NAME

wvkbd - on-screen virtual keyboard for wayland compositors using wlroots

# SYNOPSIS

wvkbd-mobintl [OPTIONS]...

*NOTE*: Your binary may have a different suffix depending on which layout you compiled.

# DESCRIPTION

This project aims to deliver a minimal but practically usable implementation of
a wlroots on-screen keyboard in legible C. This will _only_ be a keyboard, not
a feedback buzzer, led blinker, or anything that requires more than what's
needed to input text quickly. The end product should be a static codebase that
can be patched to add new features.

The native *Hide* control immediately above the keyboard's bottom-right edge
collapses it and leaves a bottom-right *Keyboard* tab. The tab reserves no
application space and restores the keyboard from any focused application.
Activation requires a press and matching release; sliding away cancels it.

## OPTIONS

*-D*
	enable debug mode.

*-o*
	print pressed keys to standard output.

*-O*
	print intersected keys to standard output.

*--mod-swipe*
	Defer ordinary character keys until release. A tap types the character once;
	a short upward/downward swipe emits Control/Alt plus it, while a short
	predominantly rightward swipe emits Control and Alt together (Emacs C-M-)
	plus it. On a primary Latin layout, a continuing horizontal or diagonal path
	inserts the best of up to three ranked local dictionary matches by comparing
	its geometry to the current keys. A rightward modifier candidate can become a
	glide only after the same two-letter, travel, horizontal-span, and trace
	requirements, so crossing neighboring keys is normal. Before glide takeover,
	the rightward candidate shows =C-M-= in the popup; during glide tracing the
	popup is empty and crossed keys are highlighted. The matches cover the
	keyboard's top row; tapping one replaces the emitted word and Space. Releasing
	outside the pressed cell cancels selection, while returning before release
	restores it. Tapping an unused candidate cell dismisses the strip. The separate
	popup remains input-empty.
	Space remains a tap. Punctuation does not glide, but retains its tap and short
	Control/Alt/Control+Alt modifier-swipe behavior; an unrecognized qualified
	glide inserts nothing. Without a working private learning sink it shows =?=;
	with a working sink supplied via *--glide-learning-fd 3* it instead shows
	=Misswipe= and =Missing= choices for explicit local attribution. The next
	ordinary input clears either surface.
	A recognized word emits one trailing Space. Before another accepted keyboard
	input, plain Backspace retracts that word and its Space. An explicit Space is
	coalesced with the emitted separator, while ordinary punctuation replaces it.
	Tapping Shift or CapsLock preserves the pending separator decision for the
	following key.
	External cursor or hardware edits are not observable.
	The gesture threshold is the floor of 40% of the key height, with a 12-pixel
	minimum. Upward/downward gestures require twice as much vertical as horizontal
	movement; Control+Alt requires positive horizontal movement at least twice the
	absolute vertical movement. This option disables hold repeat for character
	keys and cannot be combined with *-O*.

*--glide-learning-fd 3*
	Send bounded private swipe-observation datagrams through inherited descriptor
	3. The descriptor must be a connected unnamed Unix datagram socket. Invalid or
	failed descriptors disable observation without affecting keyboard input.

*-l* _layers_
	comma separated list of layers in vertical/portrait mode.

*--landscape-layers* _layers_
	comma separated list of layers used in horizontal/landscape mode.

*--list-layers*
	prints a list of all available layers.

*-H* _pixels_
	Height of the keyboard in pixels, for vertical/portrait mode.

*-L* _pixels_
	Height of the keyboard in pixels, for horizontal/landscape mode

*--fn* _font_
	set font and size (e.g. DejaVu Sans 20)

*--hidden*
	Start hidden (send SIGUSR2 to show).

*--alpha* _int_
	Set alpha value (i.e. transparency) for all colors [0-255]
	
*--bg* _rrggbb|aa_ 
	Set color of background

*--fg* _rrggbb|aa_       
	Set color of keys

*--fg-sp* _rrggbb|aa_       
	Set color of special keys

*--press* _rrggbb|aa_       
	Set color of pressed keys

*--press-sp* _rrggbb|aa_       
	Set color of pressed special keys

*--swipe* _rrggbb|aa_       
	Set color of swiped keys

*--swipe-sp* _rrggbb|aa_       
	Set color of swiped special keys

*--text* _rrggbb|aa_       
	Set color text on keys

*--text-sp* _rrggbb|aa_       
	Set color text on special keys

*--version*
	Print version information

*-h*, *--help*
	Print usage help 


# SIGNALS

You can send signals to wvkbd to hide/show it (e.g. using _kill_(1) with _-s_):

*SIGUSR1*
	Fully hide the keyboard and its native visibility tab.

*SIGUSR2*
	Show the expanded keyboard and its native visibility control.

*SIGRTMIN*
	Toggle between fully hidden and expanded. A collapsed keyboard becomes fully
	hidden.

# COMPOSE BUTTON

The default mobile international layout features a Compose button (*Cmp*)
which, when combined with another key, opens up a layout that offers variants
for that key. This is similar to functionality that other keyboards implemented
using a *long press* (wvkbd has no such notion, holding a key will repeat
it like on a physical keyboard).

For example, press Cmp + a to access variants with diacritics like á,à,â,ä, etc.. 

Most layouts also feature the following that are less obvious:

- Press Cmp and . to access more punctuation
- Press Cmp and - or , to access 'mathematical' symbols (+,-,=,etc)
- Press Cmp and ' or 0 or 9 to access more brackets and quotes
- Press Cmp and q to access emojis

Last, but not least, pressing Cmp + space or Cmp + ⌨  or Cmp + Abc opens up an
index that allows you to immediately jump to any layout by name, even layouts
not explicitly added to your layers on startup.

# AUTHORS

Created by John Sullivan <jsullivan@csumb.edu>, maintained by the Sxmo project
<https://sxmo.org> in collaboration with other open source contributors. For
more information about wvkbd development, see <https://git.sr.ht/~proycon/wvkbd>
or <https://github.com/jjsullivan5196/wvkbd>.
