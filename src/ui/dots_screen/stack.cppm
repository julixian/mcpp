// mcpp.ui.dots_screen:stack — Tetris lying on its side
// (.agents/docs/2026-09-30-build-output-refinement-design.md, §5.9 to §5.13).

export module mcpp.ui.dots_screen:stack;

import std;
import :core;

namespace mcpp::ui::dots_screen {

// ── stack: Tetris on its side. Pieces fall leftward and rest against the
// stack where they leave the fewest holes; at most three fly at once, and a
// burst of progress settles at once, so the stack's area follows the fraction
// within four pieces (design §5.11).
//
// EVERY FILL LOOP GROWS THE STACK OR STOPS. The stack keeps holes, so it can
// reach the right edge with fewer cells than the fraction asks for. A piece
// that no longer fits inside the screen is therefore not spawned, and a piece
// that adds no cell ends the loop: termination follows from the loop, not
// from the shape of the stack. Before, such a piece landed at the right edge
// on cells an earlier one held, the map of cells stopped growing, and the
// loop never ended while the ticker held the line lock, so the build hung
// after ninja (mcpp 2026.9.30.1; .agents/docs/
// 2026-09-30-build-wall-time-progress-count-and-hang-plan.md, F1). A full
// stack stays full until the build ends; the counts beside it state the build.
class Stack final : public Animation {
public:
    explicit Stack(std::uint64_t seed) : rnd_(seed) {}
    void update(const Input& in) override {
        failed_ = in.failed;
        const auto target = static_cast<std::size_t>(in.fraction * (kWidth - 6) * kHeight);
        if (!failed_) {
            while (cells_.size() + 4 * flying_.size() + 16 <= target) {
                auto piece = spawn();
                if (!piece) break;
                const auto before = cells_.size();
                lock(*piece);
                if (cells_.size() == before) break;
            }
            while (flying_.size() < 3 && cells_.size() + 4 * (flying_.size() + 1) <= target) {
                auto piece = spawn();
                if (!piece) break;
                flying_.push_back(std::move(*piece));
            }
        }
        const double speed = (5.0 + std::min(6.0, static_cast<double>(in.finished) * 0.5)) * in.dt * 10;
        for (auto it = flying_.begin(); it != flying_.end();) {
            it->x = std::max(static_cast<double>(it->land), it->x - speed);
            if (it->x <= it->land) { lock(*it); it = flying_.erase(it); }
            else ++it;
        }
    }
    void draw(Screen& cv) const override {
        for (auto const& [cell, colour] : cells_)
            cv.set(cell.first, cell.second, failed_ ? Colour::Red : colour);
        for (auto const& p : flying_)
            for (auto [dx, dy] : p.shape) cv.set(p.x + dx, dy, p.colour);
    }
private:
    using Cell = std::pair<int, int>;
    using Shape = std::vector<Cell>;
    struct Piece { Shape shape; Colour colour; double x; int land; };
    static const std::vector<std::pair<std::vector<Shape>, Colour>>& kinds() {
        static const std::vector<std::pair<std::vector<Shape>, Colour>> k = {
            {{{{0,0},{1,0},{2,0},{3,0}}, {{0,0},{0,1},{0,2},{0,3}}}, Colour::BrightCyan},
            {{{{0,0},{1,0},{0,1},{1,1}}}, Colour::Yellow},
            {{{{0,0},{1,0},{2,0},{1,1}}, {{0,0},{0,1},{0,2},{1,1}},
              {{1,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{1,2},{0,1}}}, Colour::Magenta},
            {{{{1,0},{2,0},{0,1},{1,1}}, {{0,0},{0,1},{1,1},{1,2}}}, Colour::BrightGreen},
            {{{{0,0},{1,0},{1,1},{2,1}}, {{1,0},{1,1},{0,1},{0,2}}}, Colour::Red},
            {{{{0,0},{0,1},{0,2},{1,2}}, {{0,0},{1,0},{2,0},{0,1}},
              {{0,0},{1,0},{1,1},{1,2}}, {{2,0},{0,1},{1,1},{2,1}}}, Colour::White},
            {{{{1,0},{1,1},{1,2},{0,2}}, {{0,0},{0,1},{1,1},{2,1}},
              {{0,0},{1,0},{0,1},{0,2}}, {{0,0},{1,0},{2,0},{2,1}}}, Colour::Blue},
        };
        return k;
    }
    bool taken(Cell c) const {
        if (cells_.contains(c)) return true;
        for (auto const& p : flying_)
            for (auto [dx, dy] : p.shape)
                if (Cell{p.land + dx, dy} == c) return true;
        return false;
    }
    // Where `s`, sliding in from the right, comes to rest; nothing when it
    // would rest with a cell outside the screen.
    std::optional<int> landing(const Shape& s) const {
        int x = kWidth;
        auto fits = [&](int at) {
            for (auto [dx, dy] : s) if (taken({at + dx, dy})) return false;
            return true;
        };
        while (x > 0 && fits(x - 1)) --x;
        for (auto [dx, dy] : s)
            if (x + dx >= kWidth) return std::nullopt;
        return x;
    }
    // The next piece at its best landing; nothing when no rotation and row of
    // the chosen kind lands inside the screen.
    std::optional<Piece> spawn() {
        const auto& all = kinds();
        const auto& [rotations, colour] =
            all[std::uniform_int_distribution<std::size_t>(0, all.size() - 1)(rnd_)];
        std::optional<std::pair<int, Piece>> best;
        for (auto const& r : rotations) {
            int h = 0;
            for (auto [dx, dy] : r) h = std::max(h, dy + 1);
            for (int oy = 0; oy + h <= kHeight; ++oy) {
                Shape s;
                for (auto [dx, dy] : r) s.push_back({dx, dy + oy});
                const auto landed = landing(s);
                if (!landed) continue;
                const int land = *landed;
                int front = 0, minx = kWidth;
                for (auto [dx, dy] : s) { front = std::max(front, land + dx); minx = std::min(minx, land + dx); }
                int holes = 0;
                for (auto [dx, dy] : s)
                    for (int qx = minx; qx < land + dx; ++qx)
                        if (!taken({qx, dy}) && std::ranges::find(s, Cell{qx - land, dy}) == s.end())
                            ++holes;
                const int score = holes * 4 + front;
                if (!best || score < best->first) best = {score, Piece{s, colour, double(kWidth), land}};
            }
        }
        if (!best) return std::nullopt;
        return best->second;
    }
    void lock(const Piece& p) {
        for (auto [dx, dy] : p.shape) cells_[{p.land + dx, dy}] = p.colour;
    }
    std::mt19937_64 rnd_;
    std::map<Cell, Colour> cells_;
    std::vector<Piece> flying_;
    bool failed_ = false;
};

std::unique_ptr<Animation> make_stack(std::uint64_t seed) { return std::make_unique<Stack>(seed); }

} // namespace mcpp::ui::dots_screen
