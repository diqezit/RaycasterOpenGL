

# Raycasting Engine

One of my attempts at programming something like this. There are plenty of raycaster implementations out there on github.. but so here is my version of a simple one in practicing. Let's say it's an attempt to move from C# to C++ for games.

So this is a first person rendering engine in the style of Wolfenstein 3D. The world is stored as a 2D grid of 32 by 32 cells but on screen it looks 3D thanks to the raycasting technique. Things can be tweaked and adjusted if needed.

## Screenshots

<img width="1281" height="759" alt="image" src="https://github.com/user-attachments/assets/698a5555-7692-4f76-bd35-0305637851b0" />

<img width="1281" height="759" alt="image" src="https://github.com/user-attachments/assets/f6ee7848-4c08-4cb2-850e-0d6b3ec6c288" />

## Features

Textured walls floor and ceiling with multiple texture variants. Panoramic sky that rotates with the camera.

Objects in the world as billboard sprites mushrooms trees torches. More can be added later.

Point lighting from torches with smooth falloff. Precomputed light map that takes open sky into account. Atmospheric fog that hides distant objects. Wall collisions with sliding along them. Adjustable render quality for balancing speed and detail.

## Controls

WASD movement. Mouse to look around. Shift to sprint.

## Building

Requires C++23 and raylib.

## References

Used YouTube tutorials and this project as a reference https://github.com/gonmf/raycaster
