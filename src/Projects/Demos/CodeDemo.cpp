#include "../Scenes/Media/LatexScene.h"

void render_video() {
    LatexScene cs("\\texttt{CodeScene unavailable in this build}", 0.8);
    stage_macroblock(SilenceBlock(1), 1);
    cs.render_microblock();
}
