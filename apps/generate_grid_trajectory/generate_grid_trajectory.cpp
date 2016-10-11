#include <iostream>

#include "util/system.h"
#include "util/arguments.h"

#include "mve/mesh_io_ply.h"

#include "acc/primitives.h"

#include "utp/trajectory_io.h"

struct Arguments {
    std::string proxy_mesh;
    std::string out_trajectory;
    float focal_length;
    float max_distance;
    float forward_overlap;
    float side_overlap;
};

Arguments parse_args(int argc, char **argv) {
    util::Arguments args;
    args.set_exit_on_error(true);
    args.set_nonopt_minnum(2);
    args.set_nonopt_maxnum(2);
    args.set_usage("Usage: " + std::string(argv[0]) + " [OPTS] PROXY_MESH OUT_TRAJECTORY");
    args.set_description("Generate a standard grid trajectory");
    args.add_option('\0', "focal-length", true, "camera focal length [0.86]");
    args.add_option('\0', "max-distance", true, "maximum distance to surface [80.0]");
    args.add_option('\0', "forward-overlap", true, "forward overlap in percent [80.0]");
    args.add_option('\0', "side-overlap", true, "side overlap in percent [60.0]");
    args.parse(argc, argv);

    Arguments conf;
    conf.proxy_mesh = args.get_nth_nonopt(0);
    conf.out_trajectory = args.get_nth_nonopt(1);
    conf.focal_length = 0.86f;
    conf.max_distance = 80.0f;
    conf.forward_overlap = 80.0f;
    conf.side_overlap = 60.0f;

    for (util::ArgResult const* i = args.next_option();
         i != 0; i = args.next_option()) {
        switch (i->opt->sopt) {
        case '\0':
            if (i->opt->lopt == "focal-length") {
                conf.focal_length = i->get_arg<float>();
            } else if (i->opt->lopt == "max-distance") {
                conf.max_distance = i->get_arg<float>();
            } else if (i->opt->lopt == "forward-overlap") {
                conf.forward_overlap = i->get_arg<float>();
            } else if (i->opt->lopt == "side-overlap") {
                conf.side_overlap = i->get_arg<float>();
            } else {
                throw std::invalid_argument("Invalid option");
            }
        break;
        default:
            throw std::invalid_argument("Invalid option");
        }
    }

    return conf;
}

int main(int argc, char **argv) {
    util::system::register_segfault_handler();
    util::system::print_build_timestamp(argv[0]);

    Arguments args = parse_args(argc, argv);

    mve::TriangleMesh::Ptr mesh;
    try {
        mesh = mve::geom::load_ply_mesh(args.proxy_mesh);
    } catch (std::exception& e) {
        std::cerr << "\tCould not load mesh: "<< e.what() << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::vector<math::Vec3f> const & verts = mesh->get_vertices();

    acc::AABB<math::Vec3f> aabb = acc::calculate_aabb(verts);

    float hfov = 2.0f * std::atan2(1.0f, 2.0f * args.focal_length);
    float vfov = 2.0f * std::atan2(2.0f / 3.0f, 2.0f * args.focal_length);

    float altitude = args.max_distance * 0.9f;
    float width = std::tan(hfov / 2.0f) * altitude * 2.0f;
    float height = std::tan(vfov / 2.0f) * altitude * 2.0f;

    float velocity = height * (1.0f - args.forward_overlap / 100.0f);
    float spacing = width * (1.0f - args.side_overlap / 100.0f);

    float awidth = aabb.max[0] - aabb.min[0];
    float aheight = aabb.max[1] - aabb.min[1];
    math::Vec2f center(aabb.min[0] + awidth / 2.0f, aabb.min[1] + aheight / 2.0f);

    int cols = std::ceil(awidth / spacing) + 1;
    int rows = std::ceil(aheight / velocity) + 2;

    std::vector<mve::CameraInfo> trajectory;

    mve::CameraInfo cam;

    /* Initialize nadir. */
    math::Matrix3f rot(0.0f);
    rot(0, 0) = 1;
    rot(1, 1) = -1;
    rot(2, 2) = -1;
    std::copy(rot.begin(), rot.end(), cam.rot);
    cam.flen = args.focal_length;

    for (int i = 0; i < cols; ++i) {
        float x = center[0] + spacing * (i - (cols / 2));
        for (int j = 0; j < rows; ++j) {
            float y = center[1];
            if (i % 2 == 0) {
                y += velocity * (j - (rows / 2));
            } else {
                y += velocity * ((rows / 2) - j);
            }

            math::Vec3f pos(x, y, altitude);
            math::Vec3f trans = -rot * pos;
            std::copy(trans.begin(), trans.end(), cam.trans);
            trajectory.push_back(cam);
        }

        if (i == cols - 1) break;

        float circumfence = std::acos(-1.0f) * spacing;
        int n = std::floor((circumfence / 2.0f) / velocity);
        float angle = std::acos(-1.0f) / n;
        for (int j = 0; j < n; j++) {
            float ry = std::sin(angle * j) * spacing / 2.0f;
            float rx = std::cos(angle * j) * spacing / 2.0f;
            math::Vec3f pos(x + spacing / 2.0f - rx, center[1], altitude);
            if (i % 2 == 0) {
                pos[1] += (rows / 2) * velocity + ry;
            } else {
                pos[1] -= (rows / 2) * velocity + ry;
            }
            math::Vec3f trans = -rot * pos;
            std::copy(trans.begin(), trans.end(), cam.trans);
            trajectory.push_back(cam);
        }
    }

    utp::save_trajectory(trajectory, args.out_trajectory);

    return EXIT_SUCCESS;
}
