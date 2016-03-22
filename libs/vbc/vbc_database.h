#ifndef VBC_DATABASE_H
#define VBC_DATABASE_H
#include <vector>
#include <iostream>
#include "image/image.hpp"
#include "gzip_interface.hpp"
#include "prog_interface_static_link.h"
#include "libs/tracking/tract_model.hpp"


class fib_data;
class tracking;
class TractModel;



class vbc_database
{
public:
    std::auto_ptr<fib_data> handle;
    std::string report;
    mutable std::string error_msg;
    vbc_database();
    ~vbc_database(){clear_thread();}
public:
    bool create_database(const char* templat_name);
    bool load_database(const char* database_name);
public:// database information
    float fiber_threshold;
    bool normalize_qa;
    bool output_resampling;
public:
    void calculate_spm(connectometry_result& data,stat_model& info)
    {
        ::calculate_spm(handle.get(),data,info,fiber_threshold,normalize_qa,terminated);
    }
private: // single subject analysis result
    void run_track(const tracking& fib,std::vector<std::vector<float> >& track,float seed_ratio = 1.0,unsigned int thread_count = 1);
public:// for FDR analysis
    std::vector<std::shared_ptr<std::future<void> > > threads;
    std::vector<unsigned int> subject_greater_null;
    std::vector<unsigned int> subject_lesser_null;
    std::vector<unsigned int> subject_greater;
    std::vector<unsigned int> subject_lesser;
    std::vector<float> fdr_greater,fdr_lesser;
    unsigned int total_count,total_count_null;
    unsigned int permutation_count;
    bool terminated;
public:
    std::vector<std::vector<image::vector<3,short> > > roi_list;
    std::vector<unsigned char> roi_type;
public:
    std::vector<std::string> trk_file_names;

    unsigned int length_threshold_greater,length_threshold_lesser;
    bool has_greater_result,has_lesser_result;
    float seeding_density;
    std::mutex  lock_resampling,lock_greater_tracks,lock_lesser_tracks;
    std::vector<std::shared_ptr<TractModel> > greater_tracks;
    std::vector<std::shared_ptr<TractModel> > lesser_tracks;
    std::vector<std::shared_ptr<connectometry_result> > spm_maps;
    void save_tracks_files(std::vector<std::string>&);
public:// Individual analysis
    std::vector<std::vector<float> > individual_data;
    std::vector<float> individual_data_sd;
    bool read_subject_data(const std::vector<std::string>& files,std::vector<std::vector<float> >& data);
public:// Multiple regression
    std::auto_ptr<stat_model> model;
    float tracking_threshold;
    bool use_track_length;
    float fdr_threshold,length_threshold;
    void run_permutation_multithread(unsigned int id);
    void run_permutation(unsigned int thread_count);
    void calculate_FDR(void);
    void clear_thread(void);
public:
};

#endif // VBC_DATABASE_H
