This project corresponds to the most generalizable, FOSS chess variants sandbox. Main specs:
1. Allows high-dimensional boards (so can replicate 3D and 4D chess)
2. Support for full/partial boundary geometry (periodic in x only, periodic in x with mirrored BCs, Klein-bottle geometry, 3D Klein bottle, etc.)
3. Generalizable pieces based on superset of "vector moves". "vector move" is a unit vector in the direction of move of the piece, along with number of spaces that it can move. i.e. rook moves as [1, NULL]^inf (unfolds into vector moves like k*[1, 0] + k*[0, 1] in 2D, k*[1,0,0] + k*[0,1,0] + k*[0,0,1] in 3D, etc.. Superscript corresponds to max k value); knight is [1, 2, NULL]^1, king is [1, NULL]^1 + [1, 1, NULL]^1, etc. Special moves like castling and pawn moves should also be allowed. 
4. Time-travel, for now in "extra timelines" scenario as in "5D Chess with Multiverse Time Travel"
5. Custom data fields for pieces to reproduce "explosive chess", "quantum chess", indian chess, checkers, and all possible finite-board variants

Performance is PARAMOUNT, since users might try play "5D chess + Multiverse time travel on a 5D torus", bringing number of dimensions to 7 to al in this case, for instance. Generalization and extensibility are EXTREMELY important, so that it is easy to add more features in the future (toroidal timelines? Multiple dimenions of multiverse?).

Start with 2D barebones generalizable chess implementation. 3D graphics should be supported (i.e. 3D and higher-dimensional chess boards). Do it in raw C++ with very heavy TDD, making sure that 100% of tests pass before proceeding onto the next step. Eventually we will want steam workshop support, multiplayer (thus client-server architecture), and, much-much later, custom-trainable AI models for specific chess variants. Work in nix flake environment, which you should create. Make the project very AI agent-readable with AGENTS.md file, and with CLAUDE.md just pointing to AGENTS.md.

Ask me any questions about the implementation. You must have an extremely detailed plan before you proceed with starting implementation.
Create a very detailed high-level step-by-step plan and architectural overview, and each of sub-parts of the plan should also have their own detailed plan.
