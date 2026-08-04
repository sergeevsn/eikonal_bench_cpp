#pragma once

#include <vector>
#include <cmath>
#include <iostream>
#include <chrono>
#include <numeric>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>
#include <set>
#include <array>
#include <limits>
#include <cctype>
#include <thread>

struct BenchmarkParams {
    // [grid]
    size_t nx = 101, ny = 101, nz = 101;
    double dx = 25.0, dy = 25.0, dz = 10.0;
    
    // [source]
    size_t sx = 50, sy = 50, sz = 1;

    // [receivers]
    size_t rec_step_x = 50, rec_step_y = 5;
    std::string line_axis = "Y";
    
    // [seismogram]
    double dt = 0.002;
    size_t nt = 500;
    double wave_freq = 20.0;
    
    // [velocity]
    double layer1_vel = 2000.0;
    double layer2_vel = 2800.0;
    int dome_base_z = 55;
    int dome_height_z = 25;
    int dome_radius = 40;
    int dome_smooth_z = 3;
    double refl_value = 1.0;

    // [benchmark]
    std::vector<std::string> solvers = {"eikonalfm", "thinks"};
    int eikonalfm_order = 2;
    // <= 0 → std::thread::hardware_concurrency() (как MAX_WORKERS=None в Python)
    int max_workers = 1;

    size_t get_receiver_count() const {
        size_t count = 0;
        for (size_t x = 0; x < nx; x += rec_step_x) {
            for (size_t y = 0; y < ny; y += rec_step_y) {
                count++;
            }
        }
        return count;
    }

    size_t get_line_receiver_count() const {
        if (line_axis == "X" || line_axis == "x") {
            size_t count = 0;
            for (size_t x = 0; x < nx; x += rec_step_x) count++;
            return count;
        } else {
            size_t count = 0;
            for (size_t y = 0; y < ny; y += rec_step_y) count++;
            return count;
        }
    }

    std::vector<std::array<size_t, 3>> get_fmm_stations() const {
        std::vector<std::array<size_t, 3>> stations;
        stations.push_back({sx, sy, sz});
        for (size_t x = 0; x < nx; x += rec_step_x) {
            for (size_t y = 0; y < ny; y += rec_step_y) {
                stations.push_back({x, y, 0});
            }
        }
        return stations;
    }

    void load(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Warning: Could not open " << filename << ". Using default values." << std::endl;
            return;
        }

        std::string line, section;
        while (std::getline(file, line)) {
            // Trim leading/trailing whitespace
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;

            if (line[0] == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                continue;
            }

            auto pos = line.find('=');
            if (pos == std::string::npos) continue;

            std::string key = line.substr(0, pos);
            key.erase(key.find_last_not_of(" \t") + 1);
            
            std::string value = line.substr(pos + 1);
            value.erase(0, value.find_first_not_of(" \t"));

            if (section == "grid") {
                if (key == "nx") nx = std::stoul(value);
                else if (key == "ny") ny = std::stoul(value);
                else if (key == "nz") nz = std::stoul(value);
                else if (key == "dx") dx = std::stod(value);
                else if (key == "dy") dy = std::stod(value);
                else if (key == "dz") dz = std::stod(value);
            } else if (section == "source") {
                if (key == "sx") sx = std::stoul(value);
                else if (key == "sy") sy = std::stoul(value);
                else if (key == "sz") sz = std::stoul(value);
            } else if (section == "receivers") {
                if (key == "step_x") rec_step_x = std::stoul(value);
                else if (key == "step_y") rec_step_y = std::stoul(value);
                else if (key == "line_axis") line_axis = value;
            } else if (section == "seismogram") {
                if (key == "dt") dt = std::stod(value);
                else if (key == "nt") nt = std::stoul(value);
                else if (key == "wave_freq") wave_freq = std::stod(value);
            } else if (section == "velocity") {
                if (key == "layer1_vel") layer1_vel = std::stod(value);
                else if (key == "layer2_vel") layer2_vel = std::stod(value);
                else if (key == "dome_base_z") dome_base_z = std::stoi(value);
                else if (key == "dome_height_z") dome_height_z = std::stoi(value);
                else if (key == "dome_radius") dome_radius = std::stoi(value);
                else if (key == "dome_smooth_z") dome_smooth_z = std::stoi(value);
                else if (key == "refl_value") refl_value = std::stod(value);
            } else if (section == "benchmark") {
                if (key == "solvers") {
                    solvers.clear();
                    std::stringstream ss(value);
                    std::string s;
                    while (std::getline(ss, s, ',')) {
                        s.erase(0, s.find_first_not_of(" \t"));
                        s.erase(s.find_last_not_of(" \t") + 1);
                        if (!s.empty()) solvers.push_back(s);
                    }
                }
                else if (key == "eikonalfm_order") eikonalfm_order = std::stoi(value);
                else if (key == "max_workers") max_workers = std::stoi(value);
            }
        }
        std::cout << "Loaded parameters from " << filename << std::endl;
    }
};

inline size_t available_memory_mb() {
#if defined(__linux__)
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) return 0;
    std::string key;
    size_t value_kb = 0;
    std::string unit;
    while (meminfo >> key >> value_kb >> unit) {
        if (key == "MemAvailable:") return value_kb / 1024;
    }
#endif
    return 0;
}

// Аналог run_benchmark_parallel: ограничение по RAM и авто-число потоков.
inline int resolve_max_workers(int configured, size_t mem_per_array_bytes) {
    int workers = configured;
    if (workers <= 0) {
        unsigned hw = std::thread::hardware_concurrency();
        workers = hw > 0 ? static_cast<int>(hw) : 1;
    }
    workers = std::max(1, workers);

    if (workers > 1) {
        const size_t avail_mb = available_memory_mb();
        if (avail_mb > 0) {
            const double mem_per_mb =
                static_cast<double>(mem_per_array_bytes) / (1024.0 * 1024.0);
            const double total_needed = mem_per_mb * (workers + 1);
            const double limit = static_cast<double>(avail_mb) * 0.8;
            if (total_needed > limit) {
                workers = std::max(1, static_cast<int>(limit / mem_per_mb) - 1);
            }
        }
    }
    return workers;
}

inline std::vector<double> generate_velocity_model(const BenchmarkParams& p) {
    size_t size = p.nx * p.ny * p.nz;
    std::vector<double> vel(size);

    double cx = (p.nx - 1) / 2.0;
    double cy = (p.ny - 1) / 2.0;

    for (size_t ix = 0; ix < p.nx; ++ix) {
        for (size_t iy = 0; iy < p.ny; ++iy) {
            double r = std::sqrt(std::pow(ix - cx, 2) + std::pow(iy - cy, 2));
            double rn = std::min(r / static_cast<double>(p.dome_radius), 1.0);
            double dome = (rn >= 1.0) ? 0.0 : 0.5 * (1.0 + std::cos(M_PI * rn));
            double z_iface = p.dome_base_z - p.dome_height_z * dome;

            for (size_t iz = 0; iz < p.nz; ++iz) {
                double depth = iz - z_iface;
                double half = std::max(static_cast<double>(p.dome_smooth_z), 1e-6);
                double w = 0.5 * (1.0 + std::tanh(depth / half));
                
                size_t idx = ix * p.ny * p.nz + iy * p.nz + iz;
                vel[idx] = p.layer1_vel * (1.0 - w) + p.layer2_vel * w;
            }
        }
    }
    return vel;
}

inline size_t grid_idx(const BenchmarkParams& p, size_t ix, size_t iy, size_t iz) {
    return ix * p.ny * p.nz + iy * p.nz + iz;
}

inline std::vector<std::vector<double>> get_dome_interface_z(const BenchmarkParams& p) {
    std::vector<std::vector<double>> z_iface(p.nx, std::vector<double>(p.ny));
    double cx = (p.nx - 1) / 2.0;
    double cy = (p.ny - 1) / 2.0;

    for (size_t ix = 0; ix < p.nx; ++ix) {
        for (size_t iy = 0; iy < p.ny; ++iy) {
            double r = std::sqrt(std::pow(ix - cx, 2) + std::pow(iy - cy, 2));
            double rn = std::min(r / static_cast<double>(p.dome_radius), 1.0);
            double dome = (rn >= 1.0) ? 0.0 : 0.5 * (1.0 + std::cos(M_PI * rn));
            double z = p.dome_base_z - p.dome_height_z * dome;
            z_iface[ix][iy] = std::clamp(z, 1.0, static_cast<double>(p.nz - 2));
        }
    }
    return z_iface;
}

inline std::vector<float> generate_reflectivity_model(const BenchmarkParams& p) {
    auto z_iface = get_dome_interface_z(p);
    std::vector<float> refl(p.nx * p.ny * p.nz, 0.0f);

    for (size_t ix = 0; ix < p.nx; ++ix) {
        for (size_t iy = 0; iy < p.ny; ++iy) {
            size_t iz = static_cast<size_t>(std::lround(z_iface[ix][iy]));
            refl[grid_idx(p, ix, iy, iz)] = static_cast<float>(p.refl_value);
        }
    }

    size_t cx = p.nx / 2;
    size_t cy = p.ny / 2;
    size_t max_r = std::min(cx, cy);
    const size_t taper_width = 15;
    for (size_t ix = 0; ix < p.nx; ++ix) {
        for (size_t iy = 0; iy < p.ny; ++iy) {
            double dist = std::hypot(static_cast<double>(ix) - cx, static_cast<double>(iy) - cy);
            float w = 1.0f;
            if (dist > static_cast<double>(max_r - taper_width)) {
                if (dist > static_cast<double>(max_r)) {
                    w = 0.0f;
                } else {
                    double rel = (dist - (max_r - taper_width)) / taper_width;
                    w = static_cast<float>(0.5 * (1.0 + std::cos(M_PI * std::clamp(rel, 0.0, 1.0))));
                }
            }
            for (size_t iz = 0; iz < p.nz; ++iz) {
                refl[grid_idx(p, ix, iy, iz)] *= w;
            }
        }
    }
    return refl;
}

inline std::vector<size_t> select_central_line_indices(
    const BenchmarkParams& p,
    const std::vector<std::array<size_t, 3>>& stations) {
    std::vector<size_t> rec_idx;
    for (size_t i = 1; i < stations.size(); ++i) {
        rec_idx.push_back(i);
    }

    std::string axis = p.line_axis;
    std::transform(axis.begin(), axis.end(), axis.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    std::vector<size_t> line;
    if (axis == "X") {
        size_t target = p.ny / 2;
        std::set<size_t> ys;
        for (size_t i : rec_idx) ys.insert(stations[i][1]);
        size_t best_y = *ys.begin();
        for (size_t y : ys) {
            if (std::abs(static_cast<int>(y) - static_cast<int>(target)) <
                std::abs(static_cast<int>(best_y) - static_cast<int>(target))) {
                best_y = y;
            }
        }
        for (size_t i : rec_idx) {
            if (stations[i][1] == best_y) line.push_back(i);
        }
        std::sort(line.begin(), line.end(), [&](size_t a, size_t b) {
            return stations[a][0] < stations[b][0];
        });
    } else {
        size_t target = p.nx / 2;
        std::set<size_t> xs;
        for (size_t i : rec_idx) xs.insert(stations[i][0]);
        size_t best_x = *xs.begin();
        for (size_t x : xs) {
            if (std::abs(static_cast<int>(x) - static_cast<int>(target)) <
                std::abs(static_cast<int>(best_x) - static_cast<int>(target))) {
                best_x = x;
            }
        }
        for (size_t i : rec_idx) {
            if (stations[i][0] == best_x) line.push_back(i);
        }
        std::sort(line.begin(), line.end(), [&](size_t a, size_t b) {
            return stations[a][1] < stations[b][1];
        });
    }
    return line;
}

inline std::vector<double> ricker_wavelet(double freq, double dt, size_t nt) {
    std::vector<double> wavelet(nt);
    double t0 = 1.5 / freq;
    for (size_t i = 0; i < nt; ++i) {
        double t = static_cast<double>(i) * dt;
        double term = std::pow(M_PI * freq * (t - t0), 2);
        wavelet[i] = (1.0 - 2.0 * term) * std::exp(-term);
    }
    return wavelet;
}

inline std::vector<std::vector<float>> born_forward(
    const BenchmarkParams& p,
    const std::vector<float>& refl,
    const std::vector<double>& t_src,
    const std::vector<std::vector<double>>& t_rec) {
    size_t n_rec = t_rec.size();
    std::vector<std::vector<float>> seis(p.nt, std::vector<float>(n_rec, 0.0f));
    auto wavelet = ricker_wavelet(p.wave_freq, p.dt, 100);
    const size_t lw = wavelet.size();
    const double eps = 1e-6;

    for (size_t ir = 0; ir < n_rec; ++ir) {
        for (size_t ix = 0; ix < p.nx; ++ix) {
            for (size_t iy = 0; iy < p.ny; ++iy) {
                for (size_t iz = 0; iz < p.nz; ++iz) {
                    size_t idx = grid_idx(p, ix, iy, iz);
                    float r = refl[idx];
                    if (r == 0.0f) continue;

                    double ts = t_src[idx];
                    double tr = t_rec[ir][idx];
                    double t_total = ts + tr;
                    double amp = r / (ts * tr + eps);

                    int it = static_cast<int>(t_total / p.dt);
                    if (it >= static_cast<int>(p.nt)) continue;

                    int iw0 = (it >= 0) ? 0 : -it;
                    int iw1 = std::min(static_cast<int>(lw), static_cast<int>(p.nt) - it);
                    for (int iw = iw0; iw < iw1; ++iw) {
                        seis[static_cast<size_t>(it + iw)][ir] +=
                            static_cast<float>(amp * wavelet[static_cast<size_t>(iw)]);
                    }
                }
            }
        }
    }
    return seis;
}

inline std::vector<double> compute_reflection_hodograph(
    const BenchmarkParams& p,
    const std::vector<double>& t_src,
    const std::vector<std::vector<double>>& t_rec,
    const std::vector<float>& refl,
    double wavelet_peak_delay = 0.0) {
    size_t n_rec = t_rec.size();
    std::vector<double> hod(n_rec, std::numeric_limits<double>::quiet_NaN());

    for (size_t ir = 0; ir < n_rec; ++ir) {
        double best = std::numeric_limits<double>::infinity();
        bool found = false;
        for (size_t ix = 0; ix < p.nx; ++ix) {
            for (size_t iy = 0; iy < p.ny; ++iy) {
                for (size_t iz = 0; iz < p.nz; ++iz) {
                    size_t idx = grid_idx(p, ix, iy, iz);
                    if (refl[idx] == 0.0f) continue;
                    double t = t_src[idx] + t_rec[ir][idx];
                    if (t < best) {
                        best = t;
                        found = true;
                    }
                }
            }
        }
        hod[ir] = found ? best + wavelet_peak_delay
                          : std::numeric_limits<double>::quiet_NaN();
    }
    return hod;
}

inline std::vector<std::vector<double>> extract_xz_slice(
    const BenchmarkParams& p,
    const std::vector<double>& field,
    size_t iy) {
    std::vector<std::vector<double>> slice(p.nz, std::vector<double>(p.nx));
    for (size_t ix = 0; ix < p.nx; ++ix) {
        for (size_t iz = 0; iz < p.nz; ++iz) {
            slice[iz][ix] = field[grid_idx(p, ix, iy, iz)];
        }
    }
    return slice;
}

inline std::vector<std::vector<double>> extract_xz_slice(
    const BenchmarkParams& p,
    const std::vector<float>& field,
    size_t iy) {
    std::vector<std::vector<double>> slice(p.nz, std::vector<double>(p.nx));
    for (size_t ix = 0; ix < p.nx; ++ix) {
        for (size_t iz = 0; iz < p.nz; ++iz) {
            slice[iz][ix] = field[grid_idx(p, ix, iy, iz)];
        }
    }
    return slice;
}

inline double percentile99_abs(const std::vector<std::vector<float>>& seis) {
    std::vector<float> vals;
    vals.reserve(seis.size() * (seis.empty() ? 0 : seis[0].size()));
    for (const auto& row : seis) {
        for (float v : row) vals.push_back(std::abs(v));
    }
    if (vals.empty()) return 1.0;
    size_t idx = static_cast<size_t>(0.99 * (vals.size() - 1));
    std::nth_element(vals.begin(), vals.begin() + static_cast<std::ptrdiff_t>(idx), vals.end());
    return vals[idx] > 0.0f ? static_cast<double>(vals[idx]) : 1.0;
}

inline std::vector<std::vector<float>> to_float_matrix(
    const std::vector<std::vector<double>>& m) {
    std::vector<std::vector<float>> out(m.size());
    for (size_t i = 0; i < m.size(); ++i) {
        out[i].resize(m[i].size());
        for (size_t j = 0; j < m[i].size(); ++j) {
            out[i][j] = static_cast<float>(m[i][j]);
        }
    }
    return out;
}

inline std::vector<double> thinks_to_eikonalfm_order(
    const BenchmarkParams& p, const std::vector<double>& times) {
    std::vector<double> tt(times.size());
    for (size_t ix = 0; ix < p.nx; ++ix) {
        for (size_t iy = 0; iy < p.ny; ++iy) {
            for (size_t iz = 0; iz < p.nz; ++iz) {
                size_t src_idx = grid_idx(p, ix, iy, iz);
                size_t dst_idx = ix + iy * p.nx + iz * p.nx * p.ny;
                tt[src_idx] = times[dst_idx];
            }
        }
    }
    return tt;
}

class Timer {
public:
    void start() {
        m_start = std::chrono::high_resolution_clock::now();
    }
    double stop() {
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - m_start;
        return diff.count();
    }
private:
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start;
};

class ProgressBar {
public:
    ProgressBar(size_t total, size_t width = 40) : total_(total), width_(width) {}

    void update(size_t current, const std::string& desc = "") {
        float progress = static_cast<float>(current) / total_;
        int pos = static_cast<int>(width_ * progress);

        std::cout << "\r" << desc << " [";
        for (int i = 0; i < width_; ++i) {
            if (i < pos) std::cout << "=";
            else if (i == pos) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << int(progress * 100.0) << "% (" << current << "/" << total_ << ")" << std::flush;
        
        if (current == total_) std::cout << std::endl;
    }

private:
    size_t total_;
    size_t width_;
};
