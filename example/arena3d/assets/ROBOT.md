# robot.glb

The robot of the **3D platformer demo of Godot** (`3d/platformer/player/player.glb` in
https://github.com/godotengine/godot-demo-projects), made in Blender: a skinned mesh of two parts (a gray body and
a glowing part), a skeleton of 27 joints, and the clips `idle`, `walk`, `run`, `jump`, `falling`, `shooting`,
`shooting_standing` and `RESET`. The file is the one of the demo, not changed.

Arena (`client/Models.hpp`, `makeRobot`) sets its size and its direction when it loads it, paints the glowing part
with the colour of each player, and adds a clip for dying.

The demos of Godot are distributed under the MIT licence:

> Copyright (c) 2014-present Godot Engine contributors.
> Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
> documentation files (the "Software"), to deal in the Software without restriction, including without limitation
> the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
> permit persons to whom the Software is furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all copies or substantial portions of
> the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
> WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
> OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
> OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

(The README of the demos says that the whole repository is under this licence: https://github.com/godotengine/godot-demo-projects/blob/master/LICENSE.md.
The logo of Godot itself has its own terms: it is not used here.)
