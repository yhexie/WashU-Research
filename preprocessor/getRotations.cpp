/**
  This file is responsbile for taking in cloud normals and extracting
  the 3 dominate directions of the room (x, y, and z-axis) and then
  calculating the 4 rotation matrices needed to align one of the room
  axises that is prependicular to the z-axis to the Manhattan world axises
*/

#include "getRotations.h"
#include "preprocessor.h"

#include <algorithm>
#include <eigen3/Eigen/Eigen>
#include <eigen3/Eigen/StdVector>
#include <fstream>
#include <iostream>
#include <random>

#include <omp.h>

void satoshiRansacManhattan1(const std::vector<Eigen::Vector3d> &,
                             Eigen::Vector3d &);
void satoshiRansacManhattan2(const std::vector<Eigen::Vector3d> &,
                             const Eigen::Vector3d &, Eigen::Vector3d &,
                             Eigen::Vector3d &);
void getMajorAngles(const Eigen::Vector3d &, const Eigen::Vector3d &,
                    const Eigen::Vector3d &, std::vector<Eigen::Matrix3d> &);

void getRotations(const pcl::PointCloud<NormalType>::Ptr &cloud_normals,
                  const std::string &outName, Eigen::Vector3d &M1,
                  Eigen::Vector3d &M2, Eigen::Vector3d &M3) {

  if (!FLAGS_redo) {
    std::ifstream in(outName, std::ios::in | std::ios::binary);
    if (in.is_open()) {
      std::vector<Eigen::Matrix3d> R(NUM_ROTS);
      std::vector<Eigen::Vector3d> M(3);
      for (auto &r : R)
        in.read(reinterpret_cast<char *>(r.data()), sizeof(Eigen::Matrix3d));

      for (auto &m : M)
        in.read(reinterpret_cast<char *>(m.data()), sizeof(double) * 3);

      M1 = M[0];
      M2 = M[1];
      M3 = M[2];

      in.close();

      return;
    }
  }

  // NB: Convert pcl to eigen so linear algebra is easier
  std::vector<Eigen::Vector3d> normals;
  normals.reserve(cloud_normals->size());
  for (auto &n : *cloud_normals)
    normals.emplace_back(n.normal_x, n.normal_y, n.normal_z);

  if (!FLAGS_quietMode)
    std::cout << "N size: " << normals.size() << std::endl;

  std::vector<Eigen::Vector3d> M(3);
  satoshiRansacManhattan1(normals, M[0]);
  if (!FLAGS_quietMode) {
    std::cout << "D1: " << M[0] << std::endl << std::endl;
  }

  // NB: Select normals that are perpendicular to the first
  // dominate direction
  std::vector<Eigen::Vector3d> N2;
  for (auto &n : normals)
    if (std::asin(n.cross(M[0]).norm()) > PI / 2.0 - 0.02)
      N2.push_back(n);

  if (!FLAGS_quietMode)
    std::cout << "N2 size: " << N2.size() << std::endl;

  satoshiRansacManhattan2(N2, M[0], M[1], M[2]);

  if (!FLAGS_quietMode) {
    std::cout << "D2: " << M[1] << std::endl << std::endl;
    std::cout << "D3: " << M[2] << std::endl << std::endl;
  }

  if (std::abs(M[0][2]) > 0.5) {
    M1 = M[0];
    M2 = M[1];
  } else if (std::abs(M[1][2]) > 0.5) {
    M1 = M[1];
    M2 = M[0];
  } else {
    M1 = M[2];
    M2 = M[0];
  }

  if (M1[2] < 0)
    M1 *= -1.0;

  M3 = M1.cross(M2);

  M[0] = M1;
  M[1] = M2;
  M[2] = M3;

  std::vector<Eigen::Matrix3d> R(4);

  getMajorAngles(M1, M2, M3, R);

  if (!FLAGS_quietMode) {
    for (auto &r : R)
      std::cout << r << std::endl << std::endl;
  }

  if (FLAGS_save) {
    std::ofstream binaryWriter(outName, std::ios::out | std::ios::binary);
    for (auto &r : R)
      binaryWriter.write(reinterpret_cast<const char *>(r.data()),
                         sizeof(Eigen::Matrix3d));

    for (auto &m : M)
      binaryWriter.write(reinterpret_cast<const char *>(m.data()),
                         sizeof(double) * 3);

    binaryWriter.close();
  }
}

#pragma omp declare reduction(+ : Eigen::Vector3d : omp_out += omp_in)

/**
  Gets the first dominate direction.  Dominate direction extraction
  is done using RANSAC.  N is all normals and M is the ouput
*/
void satoshiRansacManhattan1(const std::vector<Eigen::Vector3d> &N,
                             Eigen::Vector3d &M) {
  const int m = N.size();

  volatile double maxInliers = 0, K = 1e5;
  volatile int k = 0;

  static std::random_device seed;
  static std::mt19937_64 gen(seed());
  std::uniform_int_distribution<int> dist(0, m - 1);

  while (k < K) {
    // random sampling
    int randomIndex = dist(gen);
    // compute the model parameters
    const Eigen::Vector3d &nest = N[randomIndex];

    // Count the number of inliers
    double numInliers = 0;
    Eigen::Vector3d average = Eigen::Vector3d::Zero();
#pragma omp parallel
    {
      double privateInliers = 0;
      Eigen::Vector3d privateAve = Eigen::Vector3d::Zero();
#pragma omp for nowait schedule(static)
      for (int i = 0; i < m; ++i) {
        auto &n = N[i];
        // nest and n are both unit vectors, so this is |angle|
        // between them
        if (std::acos(std::abs(nest.dot(n))) < 0.02) {
          ++privateInliers;
          // NB: All normals that are inliers with the estimate
          // are averaged together to get the best estimate
          // of the dominate direction
          if (nest.dot(n) < 0)
            privateAve -= n;
          else
            privateAve += n;
        }
      }

#pragma omp for schedule(static) ordered
      for (int i = 0; i < omp_get_num_threads(); ++i) {
#pragma omp ordered
        {
          average += privateAve;
          numInliers += privateInliers;
        }
      }
    }

    if (numInliers > maxInliers) {
      maxInliers = numInliers;

      M = average / average.norm();
      // NB: Ransac formula to check for consensus
      double w = (numInliers - 3) / m;
      double p = std::max(0.001, std::pow(w, 3));
      K = log(1 - 0.999) / log(1 - p);
    }
    ++k;
  }
}

/**
  Extracts the remaining two dominate directions simultaneously.
  N is all normals perpendicular to n1, the first dominate direction.
  M1 and M2 are the outputs for the two remaining dominate directions.
  This method follows a very similar the first version of it
*/
void satoshiRansacManhattan2(const std::vector<Eigen::Vector3d> &N,
                             const Eigen::Vector3d &n1, Eigen::Vector3d &M1,
                             Eigen::Vector3d &M2) {
  const int m = N.size();

  volatile double maxInliers = 0, K = 1.0e5;
  volatile int k = 0;

  static std::random_device seed;
  static std::mt19937_64 gen(seed());
  std::uniform_int_distribution<int> dist(0, m - 1);

  while (k < K) {
    // random sampling
    int randomIndex = dist(gen);
    // compute the model parameters
    const Eigen::Vector3d &nest = N[randomIndex];

    const Eigen::Vector3d nest2 = nest.cross(n1);

    // counting inliers and outliers
    double numInliers = 0;
    Eigen::Vector3d average = Eigen::Vector3d::Zero();
#pragma omp parallel
    {
      double privateInliers = 0;
      Eigen::Vector3d privateAve = Eigen::Vector3d::Zero();
      Eigen::Vector3d x;

#pragma omp for nowait schedule(static)
      for (int i = 0; i < m; ++i) {
        auto &n = N[i];
        if (std::min(std::acos(std::abs(nest.dot(n))),
                     std::acos(std::abs(nest2.dot(n)))) < 0.02) {
          if (std::acos(std::abs(nest.dot(n))) < 0.02)
            x = n;
          else
            x = n.cross(n1);

          if (nest.dot(x) < 0)
            privateAve -= x;
          else
            privateAve += x;

          ++privateInliers;
        }
      }
#pragma omp for schedule(static) ordered
      for (int i = 0; i < omp_get_num_threads(); ++i) {
#pragma omp ordered
        {
          average += privateAve;
          numInliers += privateInliers;
        }
      }
    }

    if (numInliers > maxInliers) {
      maxInliers = numInliers;

      average /= average.norm();
      M1 = average;
      M2 = average.cross(n1);

      double w = (maxInliers - 3) / m;
      double p = std::max(0.001, std::pow(w, 3));
      K = log(1 - 0.999) / log(1 - p);
    }
    ++k;
  }
}

void getMajorAngles(const Eigen::Vector3d &M1, const Eigen::Vector3d &M2,
                    const Eigen::Vector3d &M3,
                    std::vector<Eigen::Matrix3d> &R) {
  /* List of possible dominate directions that could be the x-axis.
    It's complimentary y-axis is just the next axis in the list.
  */
  const Eigen::Vector3d axises[] = {M2, M3, -M2, -M3};
  auto &z = M1;

  for (int i = 0; i < NUM_ROTS; ++i) {
    Eigen::Matrix3d out;
    auto &x = axises[i];
    auto &y = axises[(i + 1) % NUM_ROTS];

    for (int j = 0; j < 3; ++j) {
      out(0, j) = x[j];
      out(1, j) = y[j];
      out(2, j) = z[j];
    }

    R[i] = out;
    /*for (int j = 0; j < 3; ++j) {
      R[i](2, j) = -R[i](2, j);
      R[i](1, j) = -R[i](1, j);
    }*/
  }
}