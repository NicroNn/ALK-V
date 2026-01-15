package alkv.bytecode;

import java.util.ArrayList;
import java.util.List;

public final class Label {
    int position = -1;                 // индекс инструкции (pc)
    final List<Integer> patchSites = new ArrayList<>(); // pc мест, где надо пропатчить sBx
}
