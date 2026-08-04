#include <iostream>
#include <vector>
#include <iomanip>
#include <array>
#include <algorithm>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
#include <thread>
#include "benchmark_utils.hpp"

// eikonalfm
#include "marcher.hpp"

// thinks
#include "thinks/fast_marching_method/fast_marching_method.hpp"

// matplotplusplus
#include <matplot/matplot.h>

struct TimingResult {
    double total_wall;      // wall time цикла (solve + копирование TT)
    double total_solve;       // сумма per-source таймеров (только solve)
    double avg_per_source;    // total_solve / n_sources
    double overhead;          // total_wall - total_solve
};

struct SolverResult {
    TimingResult timing;
    std::map<size_t, std::vector<double>> tt_by_station;
};

namespace {
constexpr float kQcFontSize = 13.f;
constexpr float kQcTitleScale = 1.15f;

void apply_qc_axis_style(matplot::axes_handle ax, bool equal_aspect = false) {
    ax->font_size(kQcFontSize);
    ax->title_font_size_multiplier(kQcTitleScale);
    ax->axes_aspect_ratio_auto(!equal_aspect);
}

// Ручная раскладка панелей: без больших полей matplot subplot по умолчанию.
std::array<float, 4> qc_panel_position(size_t col, size_t ncols, float gap = 0.025f) {
    const float left = 0.05f;
    const float bottom = 0.14f;
    const float top = 0.90f;
    const float right = 0.98f;
    const float w = (right - left - gap * static_cast<float>(ncols - 1)) / static_cast<float>(ncols);
    const float x = left + static_cast<float>(col) * (w + gap);
    return {x, bottom, w, top - bottom};
}
}  // namespace

SolverResult benchmark_eikonalfm(
    const BenchmarkParams& p,
    const std::vector<double>& vel,
    const std::vector<std::array<size_t, 3>>& stations,
    const std::set<size_t>& keep,
    int max_workers) {
    const size_t n = stations.size();
    usize shape[3] = { (usize)p.nx, (usize)p.ny, (usize)p.nz };
    double dx[3] = { p.dx, p.dy, p.dz };

    std::vector<double> individual_times(n);
    ProgressBar pb(n);
    const std::string label = max_workers > 1 ? "EikonalFM" : "EikonalFM[seq]";

    Timer total_timer;
    SolverResult result;
    std::mutex tt_mutex;

    total_timer.start();

    if (max_workers <= 1) {
        MarcherInfo info(3, shape);
        Marcher marcher(vel.data(), info, dx, p.eikonalfm_order);
        std::vector<double> tau(info.size);

        for (size_t i = 0; i < n; ++i) {
            size_t source_idx = grid_idx(p, stations[i][0], stations[i][1], stations[i][2]);

            Timer t;
            t.start();
            marcher.solve(source_idx, tau.data());
            individual_times[i] = t.stop();

            if (keep.count(i)) {
                result.tt_by_station[i] = tau;
            }

            if ((i + 1) % 4 == 0 || i + 1 == n) {
                pb.update(i + 1, label);
            }
        }
    } else {
        std::atomic<size_t> next{0};
        std::atomic<size_t> done{0};
        std::mutex progress_mutex;

        auto worker = [&]() {
            MarcherInfo info(3, shape);
            Marcher marcher(vel.data(), info, dx, p.eikonalfm_order);
            std::vector<double> tau(info.size);

            while (true) {
                const size_t i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= n) break;

                const size_t source_idx =
                    grid_idx(p, stations[i][0], stations[i][1], stations[i][2]);

                Timer t;
                t.start();
                marcher.solve(source_idx, tau.data());
                individual_times[i] = t.stop();

                if (keep.count(i)) {
                    std::lock_guard<std::mutex> lock(tt_mutex);
                    result.tt_by_station[i] = tau;
                }

                const size_t completed = done.fetch_add(1, std::memory_order_relaxed) + 1;
                if (completed % 4 == 0 || completed == n) {
                    std::lock_guard<std::mutex> lock(progress_mutex);
                    pb.update(completed, label);
                }
            }
        };

        std::vector<std::thread> threads;
        threads.reserve(static_cast<size_t>(max_workers));
        for (int w = 0; w < max_workers; ++w) {
            threads.emplace_back(worker);
        }
        for (auto& th : threads) {
            th.join();
        }
    }

    double wall_time = total_timer.stop();

    double sum = std::accumulate(individual_times.begin(), individual_times.end(), 0.0);
    const double overhead = (max_workers <= 1) ? (wall_time - sum) : 0.0;
    result.timing = {wall_time, sum, sum / n, overhead};
    return result;
}

SolverResult benchmark_thinks(
    const BenchmarkParams& p,
    const std::vector<double>& vel,
    const std::vector<std::array<size_t, 3>>& stations,
    const std::set<size_t>& keep,
    int max_workers) {
    const size_t n = stations.size();
    std::array<size_t, 3> grid_size = { p.nx, p.ny, p.nz };
    std::array<double, 3> grid_spacing = { p.dx, p.dy, p.dz };

    std::vector<double> vel_thinks(vel.size());
    for (size_t ix = 0; ix < p.nx; ++ix) {
        for (size_t iy = 0; iy < p.ny; ++iy) {
            for (size_t iz = 0; iz < p.nz; ++iz) {
                size_t src_idx = grid_idx(p, ix, iy, iz);
                size_t dst_idx = ix + iy * p.nx + iz * p.nx * p.ny;
                vel_thinks[dst_idx] = vel[src_idx];
            }
        }
    }

    std::vector<double> individual_times(n);
    ProgressBar pb(n);
    const std::string label = max_workers > 1 ? "thinks" : "thinks[seq]";

    Timer total_timer;
    SolverResult result;
    std::mutex tt_mutex;

    total_timer.start();

    if (max_workers <= 1) {
        thinks::fast_marching_method::VaryingSpeedEikonalSolver<double, 3> solver(
            grid_spacing, grid_size, vel_thinks);

        for (size_t i = 0; i < n; ++i) {
            std::vector<std::array<int32_t, 3>> boundary_indices = {{
                static_cast<int32_t>(stations[i][0]),
                static_cast<int32_t>(stations[i][1]),
                static_cast<int32_t>(stations[i][2])
            }};
            std::vector<double> boundary_times = { 0.0 };

            Timer t;
            t.start();
            auto times = thinks::fast_marching_method::SignedArrivalTime(
                grid_size, boundary_indices, boundary_times, solver);
            individual_times[i] = t.stop();

            if (keep.count(i)) {
                result.tt_by_station[i] = thinks_to_eikonalfm_order(p, times);
            }

            if ((i + 1) % 4 == 0 || i + 1 == n) {
                pb.update(i + 1, label);
            }
        }
    } else {
        std::atomic<size_t> next{0};
        std::atomic<size_t> done{0};
        std::mutex progress_mutex;

        auto worker = [&]() {
            thinks::fast_marching_method::VaryingSpeedEikonalSolver<double, 3> solver(
                grid_spacing, grid_size, vel_thinks);

            while (true) {
                const size_t i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= n) break;

                std::vector<std::array<int32_t, 3>> boundary_indices = {{
                    static_cast<int32_t>(stations[i][0]),
                    static_cast<int32_t>(stations[i][1]),
                    static_cast<int32_t>(stations[i][2])
                }};
                std::vector<double> boundary_times = { 0.0 };

                Timer t;
                t.start();
                auto times = thinks::fast_marching_method::SignedArrivalTime(
                    grid_size, boundary_indices, boundary_times, solver);
                individual_times[i] = t.stop();

                if (keep.count(i)) {
                    std::lock_guard<std::mutex> lock(tt_mutex);
                    result.tt_by_station[i] = thinks_to_eikonalfm_order(p, times);
                }

                const size_t completed = done.fetch_add(1, std::memory_order_relaxed) + 1;
                if (completed % 4 == 0 || completed == n) {
                    std::lock_guard<std::mutex> lock(progress_mutex);
                    pb.update(completed, label);
                }
            }
        };

        std::vector<std::thread> threads;
        threads.reserve(static_cast<size_t>(max_workers));
        for (int w = 0; w < max_workers; ++w) {
            threads.emplace_back(worker);
        }
        for (auto& th : threads) {
            th.join();
        }
    }

    double wall_time = total_timer.stop();

    double sum = std::accumulate(individual_times.begin(), individual_times.end(), 0.0);
    const double overhead = (max_workers <= 1) ? (wall_time - sum) : 0.0;
    result.timing = {wall_time, sum, sum / n, overhead};
    return result;
}

void plot_model_qc(
    const BenchmarkParams& p,
    const std::vector<double>& vel,
    const std::vector<float>& refl) {
    using namespace matplot;

    size_t slice_y = p.ny / 2;
    double x_max = (p.nx - 1) * p.dx;
    double z_max = (p.nz - 1) * p.dz;

    auto v_slice = extract_xz_slice(p, vel, slice_y);
    auto r_slice = extract_xz_slice(p, refl, slice_y);
    double r_abs = 0.0;
    for (const auto& row : r_slice) {
        for (double v : row) r_abs = std::max(r_abs, std::abs(v));
    }
    if (r_abs < 1e-12) r_abs = 1.0;

    auto f = figure(true);
    f->size(1400, 520);
    f->font_size(kQcFontSize);

    constexpr float kModelPanelGap = 0.08f;

    auto ax0 = subplot(f, qc_panel_position(0, 2, kModelPanelGap));
    apply_qc_axis_style(ax0);
    imagesc(ax0, 0, x_max, 0, z_max, v_slice);
    ax0->colormap(palette::viridis());
    ax0->cblim({p.layer1_vel, p.layer2_vel});
    ax0->title("Velocity (XZ, Y center)");
    ax0->xlabel("X (m)");
    ax0->ylabel("Z (m)");
    ax0->cblabel("Vp (m/s)");
    colorbar(ax0);

    auto ax1 = subplot(f, qc_panel_position(1, 2, kModelPanelGap));
    apply_qc_axis_style(ax1);
    imagesc(ax1, 0, x_max, 0, z_max, r_slice);
    ax1->colormap(palette::gray());
    ax1->cblim({-r_abs, r_abs});
    ax1->title("Reflectivity (XZ, Y center)");
    ax1->xlabel("X (m)");
    ax1->cblabel("R");
    colorbar(ax1);

    sgtitle("Model QC: central XZ slices");
    save("model_qc_xz.png");
    std::cout << "Saved model_qc_xz.png" << std::endl;
}

void draw_survey_map(
    matplot::axes_handle ax,
    const BenchmarkParams& p,
    const std::vector<std::array<size_t, 3>>& stations,
    const std::vector<size_t>& line_idx) {
    using namespace matplot;

    apply_qc_axis_style(ax, true);

    double x_max = (p.nx - 1) * p.dx;
    double y_max = (p.ny - 1) * p.dy;

    ax->hold(true);

    std::vector<double> outline_x = {0, x_max, x_max, 0, 0};
    std::vector<double> outline_y = {0, 0, y_max, y_max, 0};
    auto outline = plot(ax, outline_x, outline_y, "-");
    outline->color({0.6f, 0.6f, 0.6f});
    outline->display_name("Model outline");

    std::vector<double> rec_x, rec_y;
    for (size_t i = 1; i < stations.size(); ++i) {
        rec_x.push_back(stations[i][0] * p.dx);
        rec_y.push_back(stations[i][1] * p.dy);
    }
    auto recs = scatter(ax, rec_x, rec_y);
    recs->marker_size(12.0);
    recs->marker_color({0.55f, 0.55f, 0.55f});
    recs->marker_face_color({0.55f, 0.55f, 0.55f});
    recs->display_name("Receivers (n=" + std::to_string(rec_x.size()) + ")");

    std::vector<double> line_x, line_y;
    for (size_t idx : line_idx) {
        line_x.push_back(stations[idx][0] * p.dx);
        line_y.push_back(stations[idx][1] * p.dy);
    }
    auto line = plot(ax, line_x, line_y, "-");
    line->color({0.77f, 0.31f, 0.32f});
    line->line_width(2.0);
    line->display_name("Seismogram line (" + p.line_axis + ")");

    auto qc = scatter(ax, line_x, line_y);
    qc->marker_size(28.0);
    qc->marker_color({0.77f, 0.31f, 0.32f});
    qc->marker_face_color({0.77f, 0.31f, 0.32f});
    qc->display_name("QC receivers (n=" + std::to_string(line_x.size()) + ")");

    auto src = scatter(ax, std::vector<double>{p.sx * p.dx}, std::vector<double>{p.sy * p.dy});
    src->marker_size(25.0);
    src->marker_style(line_spec::marker_style::pentagram);
    src->marker_color({0.87f, 0.52f, 0.32f});
    src->marker_face_color({0.87f, 0.52f, 0.32f});
    src->display_name("Source (" + std::to_string(p.sx) + "," + std::to_string(p.sy) + ")");

    ax->axis(equal);
    ax->xlim({0, x_max});
    ax->ylim({0, y_max});
    ax->xlabel("X (m)");
    ax->ylabel("Y (m)");
    ax->title("Survey geometry (plan view)");
    ax->grid(true);
    ax->legend();
}

void draw_seismogram(
    matplot::axes_handle ax,
    const BenchmarkParams& p,
    const std::vector<std::vector<float>>& seis,
    const std::string& title,
    const std::vector<double>& hodograph = {}) {
    using namespace matplot;

    apply_qc_axis_style(ax);

    std::vector<std::vector<double>> seis_d(seis.size());
    for (size_t i = 0; i < seis.size(); ++i) {
        seis_d[i].resize(seis[i].size());
        for (size_t j = 0; j < seis[i].size(); ++j) {
            seis_d[i][j] = seis[i][j];
        }
    }

    double vmax = percentile99_abs(seis);
    double t_max = p.nt * p.dt;
    double x_max = seis.empty() ? 0.0 : static_cast<double>(seis[0].size());

    imagesc(ax, 0, x_max, 0, t_max, seis_d);
    ax->colormap(palette::gray());
    ax->cblim({-vmax, vmax});

    if (!hodograph.empty()) {
        std::vector<double> x(hodograph.size()), y = hodograph;
        for (size_t i = 0; i < hodograph.size(); ++i) x[i] = i + 0.5;
        hold(ax, true);
        auto hod = plot(ax, x, y, "--");
        hod->color({0.84f, 0.15f, 0.16f});
        hod->line_width(1.4);
        hod->display_name("reference hodograph");
        ax->legend();
    }

    ax->xlim({0, x_max});
    ax->ylim({0, t_max});

    ax->title(title);
    ax->xlabel("Receiver Index");
    ax->ylabel("Time (s)");
}

void plot_qc_combined_row(
    const BenchmarkParams& p,
    const std::vector<std::array<size_t, 3>>& stations,
    const std::vector<size_t>& line_idx,
    const std::vector<std::string>& solver_order,
    const std::map<std::string, std::vector<std::vector<float>>>& seis_by_solver,
    const std::map<std::string, std::string>& solver_titles,
    const std::vector<double>& hodograph) {
    using namespace matplot;

    size_t ncols = 1 + solver_order.size();
    auto f = figure(true);
    // Умеренный размер + крупные шрифты (gnuplot держит pt-размер текста при росте PNG).
    f->size(static_cast<int>(560 * ncols), 680);
    f->font_size(kQcFontSize);

    draw_survey_map(subplot(f, qc_panel_position(0, ncols)), p, stations, line_idx);

    for (size_t si = 0; si < solver_order.size(); ++si) {
        const auto& name = solver_order[si];
        std::string title = solver_titles.count(name) ? solver_titles.at(name) : name;
        draw_seismogram(
            subplot(f, qc_panel_position(si + 1, ncols)),
            p,
            seis_by_solver.at(name),
            "Seismogram (" + title + ")",
            hodograph);
    }

    save("qc_combined_row.png");
    std::cout << "Saved qc_combined_row.png" << std::endl;
}

int main() {
    BenchmarkParams p;
    p.load("../benchmark.ini");

    auto stations = p.get_fmm_stations();
    auto line_idx = select_central_line_indices(p, stations);
    std::set<size_t> keep_idx = {0};
    keep_idx.insert(line_idx.begin(), line_idx.end());

    std::cout << "--- Realistic Survey Benchmark (C++) ---" << std::endl;
    std::cout << "Grid: (" << p.nx << ", " << p.ny << ", " << p.nz << ")" << std::endl;
    std::cout << "Velocity: " << p.layer1_vel << "/" << p.layer2_vel << " m/s, "
              << "dome base/height Z=" << p.dome_base_z << "/" << p.dome_height_z << std::endl;

    std::cout << "Solvers: [";
    for (size_t i = 0; i < p.solvers.size(); ++i) {
        std::cout << "'" << p.solvers[i] << "'" << (i == p.solvers.size() - 1 ? "" : ", ");
    }
    std::cout << "]" << std::endl;

    std::cout << "FMM stations (source+receivers): " << stations.size() << std::endl;
    std::cout << "Seismogram line (" << p.line_axis << ") from same receivers: " << line_idx.size() << std::endl;

    const size_t grid_bytes = p.nx * p.ny * p.nz * sizeof(double);
    const int max_workers = resolve_max_workers(p.max_workers, grid_bytes);
    std::cout << "Parallel workers: " << max_workers
              << (max_workers > 1 ? " (thread pool)" : " (sequential)") << std::endl;
    std::cout << "----------------------------------\n" << std::endl;

    std::cout << "Generating velocity model..." << std::endl;
    auto vel = generate_velocity_model(p);
    auto refl = generate_reflectivity_model(p);

    plot_model_qc(p, vel, refl);

    std::map<std::string, TimingResult> timings;
    std::map<std::string, std::map<size_t, std::vector<double>>> tt_by_solver;
    std::map<std::string, std::vector<std::vector<float>>> seis_by_solver;
    std::map<std::string, std::string> solver_titles = {
        {"eikonalfm", "EikonalFM"},
        {"thinks", "thinks"}
    };

    std::vector<double> hodograph;
    const double wavelet_peak_delay = 1.5 / p.wave_freq;

    std::cout << "Running eikonal solvers + collecting travel times for seismogram line..." << std::endl;

    for (const auto& name : p.solvers) {
        SolverResult res;
        if (name == "eikonalfm") {
            res = benchmark_eikonalfm(p, vel, stations, keep_idx, max_workers);
        } else if (name == "thinks") {
            res = benchmark_thinks(p, vel, stations, keep_idx, max_workers);
        } else {
            std::cout << "Unknown solver: " << name << std::endl;
            continue;
        }

        timings[name] = res.timing;
        tt_by_solver[name] = std::move(res.tt_by_station);
        std::string title = solver_titles.count(name) ? solver_titles[name] : name;
        std::cout << title << " Total: " << std::fixed << std::setprecision(2) << res.timing.total_wall << "s, "
                  << "Avg per source: " << std::fixed << std::setprecision(4) << res.timing.avg_per_source << "s";
        if (max_workers <= 1) {
            std::cout << ", Solve: " << std::fixed << std::setprecision(2) << res.timing.total_solve << "s"
                      << " (overhead " << std::fixed << std::setprecision(2) << res.timing.overhead << "s)";
        } else {
            std::cout << " (cpu_solve " << std::fixed << std::setprecision(2) << res.timing.total_solve << "s)";
        }
        std::cout << std::endl;

        std::vector<std::vector<double>> t_rec;
        for (size_t idx : line_idx) {
            t_rec.push_back(tt_by_solver[name].at(idx));
        }
        seis_by_solver[name] = born_forward(p, refl, tt_by_solver[name].at(0), t_rec);

        if (hodograph.empty()) {
            hodograph = compute_reflection_hodograph(
                p, tt_by_solver[name].at(0), t_rec, refl, wavelet_peak_delay);
        }
    }

    if (!seis_by_solver.empty()) {
        plot_qc_combined_row(p, stations, line_idx, p.solvers, seis_by_solver, solver_titles, hodograph);
    }

    if (!p.solvers.empty() && tt_by_solver.size() > 1) {
        std::string ref_name = p.solvers[0];
        std::string ref_title = solver_titles.count(ref_name) ? solver_titles.at(ref_name) : ref_name;
        const auto& ref_tt = tt_by_solver.at(ref_name);

        std::cout << "\nRMSE Eikonal Time vs " << ref_title << ":" << std::endl;
        for (const auto& name : p.solvers) {
            if (name == ref_name || !tt_by_solver.count(name)) continue;

            double sq = 0.0;
            size_t n = 0;
            for (size_t idx : keep_idx) {
                const auto& tt = tt_by_solver.at(name).at(idx);
                const auto& ref = ref_tt.at(idx);
                for (size_t i = 0; i < tt.size(); ++i) {
                    double diff = tt[i] - ref[i];
                    sq += diff * diff;
                }
                n += tt.size();
            }
            double rmse = (n > 0) ? std::sqrt(sq / static_cast<double>(n))
                                  : std::numeric_limits<double>::quiet_NaN();
            std::string title = solver_titles.count(name) ? solver_titles.at(name) : name;
            std::cout << "  " << title << ": " << std::scientific << std::setprecision(6) << rmse << std::endl;
        }
    }

    if (!p.solvers.empty()) {
        std::string ref_name = p.solvers[0];
        double base_time = timings[ref_name].total_wall;

        std::cout << "\n--- Timing Summary ---" << std::endl;
        for (const auto& name : p.solvers) {
            if (timings.count(name)) {
                auto res = timings[name];
                double rel = (base_time > 0) ? res.total_wall / base_time : 0;
                std::string title = solver_titles.count(name) ? solver_titles[name] : name;
                std::cout << "  " << std::left << std::setw(12) << title << "  "
                          << "total=" << std::right << std::setw(8) << std::fixed << std::setprecision(2) << res.total_wall << "s  "
                          << "avg=" << std::right << std::setw(7) << std::fixed << std::setprecision(4) << res.avg_per_source << "s  ";
                if (max_workers <= 1) {
                    std::cout << "solve=" << std::right << std::setw(8) << std::fixed << std::setprecision(2) << res.total_solve << "s  ";
                }
                std::cout << "rel_to_" << ref_name << "=" << std::fixed << std::setprecision(2) << rel << "x" << std::endl;
            }
        }
    }

    return 0;
}
