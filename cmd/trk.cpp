#include <QFileInfo>
#include <QStringList>
#include <QDir>
#include <iostream>
#include <iterator>
#include <string>
#include "tipl/tipl.hpp"
#include "tracking/region/Regions.h"
#include "libs/tracking/tract_model.hpp"
#include "libs/tracking/tracking_thread.hpp"
#include "fib_data.hpp"
#include "libs/gzip_interface.hpp"
#include "mapping/atlas.hpp"
#include "SliceModel.h"
#include "connectometry/group_connectometry_analysis.h"
#include "program_option.hpp"
extern std::shared_ptr<CustomSliceModel> t1t2_slices;
std::shared_ptr<CustomSliceModel> t1t2_slices;
extern std::vector<std::shared_ptr<CustomSliceModel> > other_slices;
std::vector<std::shared_ptr<CustomSliceModel> > other_slices;

bool atl_load_atlas(const std::string atlas_name,std::vector<std::shared_ptr<atlas> >& atlas_list);
void check_other_slices(std::shared_ptr<fib_data> handle)
{
    if(!po.has("other_slices") || !other_slices.empty())
        return;
    QStringList slice_list = QString(po.get("other_slices").c_str()).split(",");
    for(int i = 0;i < slice_list.size();++i)
    {
        other_slices.push_back(std::make_shared<CustomSliceModel>(handle.get()));
        std::vector<std::string> files;
        files.push_back(slice_list[i].toStdString());
        std::cout << "add other slice: " << QFileInfo(slice_list[i]).baseName().toStdString() << std::endl;
        if(!other_slices.back()->initialize(files))
        {
            std::cout << "ERROR: fail to load " << files.back() << std::endl;
            return;
        }
        if(other_slices.back()->thread.get())
            other_slices.back()->thread->wait();
    }
}
bool get_t1t2_nifti(std::shared_ptr<fib_data> handle,
                    tipl::geometry<3>& nifti_geo,
                    tipl::vector<3>& nifti_vs,
                    tipl::matrix<4,4,float>& convert)
{
    if(!po.has("t1t2"))
        return false;
    if(!t1t2_slices.get())
    {
        t1t2_slices = std::make_shared<CustomSliceModel>(handle.get());
        std::vector<std::string> files;
        files.push_back(po.get("t1t2"));
        if(!t1t2_slices->initialize(files))
        {
            std::cout << "ERROR: fail to load " << files.back() << std::endl;
            return false;
        }
        handle->view_item.pop_back(); // remove the new item added by initialize
        if(t1t2_slices->thread.get())
            t1t2_slices->thread->wait();
        std::cout << "registeration complete" << std::endl;
        std::cout << convert[0] << " " << convert[1] << " " << convert[2] << " " << convert[3] << std::endl;
        std::cout << convert[4] << " " << convert[5] << " " << convert[6] << " " << convert[7] << std::endl;
        std::cout << convert[8] << " " << convert[9] << " " << convert[10] << " " << convert[11] << std::endl;
    }
    nifti_geo = t1t2_slices->source_images.geometry();
    nifti_vs = t1t2_slices->vs;
    convert = t1t2_slices->invT;
    std::cout << "T1T2 dimension: " << nifti_geo << std::endl;
    std::cout << "T1T2 voxel size: " << nifti_vs << std::endl;
    return true;
}
void export_track_info(std::shared_ptr<fib_data> handle,
                       std::shared_ptr<TractModel> tract_model)
{
    std::string export_option = po.get("export");
    std::string file_name = po.get("output",po.get("tract",po.get("source")+"tt.gz"));
    std::replace(export_option.begin(),export_option.end(),',',' ');
    std::istringstream in(export_option);
    std::string cmd;

    while(in >> cmd)
    {
        // track analysis report
        if(cmd.find("report") == 0)
        {
            std::cout << "export track analysis report..." << std::endl;
            std::replace(cmd.begin(),cmd.end(),':',' ');
            std::istringstream in(cmd);
            std::string report_tag,index_name;
            uint32_t profile_dir = 0,bandwidth = 0;
            in >> report_tag >> index_name >> profile_dir >> bandwidth;
            std::vector<float> values,data_profile,data_ci1,data_ci2;
            // check index
            if(index_name != "qa" && index_name != "fa" &&  handle->get_name_index(index_name) == handle->view_item.size())
            {
                std::cout << "cannot find index name:" << index_name << std::endl;
                continue;
            }
            if(bandwidth == 0)
            {
                std::cout << "please specify bandwidth value" << std::endl;
                continue;
            }
            if(profile_dir > 4)
            {
                std::cout << "please specify a valid profile type" << std::endl;
                continue;
            }
            std::cout << "calculating report" << std::endl;
            std::cout << "profile_dir:" << profile_dir << std::endl;
            std::cout << "bandwidth:" << bandwidth << std::endl;
            std::cout << "index_name:" << index_name << std::endl;
            tract_model->get_report(
                                handle,
                                profile_dir,
                                bandwidth,
                                index_name,
                                values,data_profile,data_ci1,data_ci2);

            std::replace(cmd.begin(),cmd.end(),' ','.');
            std::string file_name_stat = file_name + "." + cmd + ".txt";
            std::cout << "output report:" << file_name_stat << std::endl;
            std::ofstream report(file_name_stat.c_str());
            report << "position\t";
            std::copy(values.begin(),values.end(),std::ostream_iterator<float>(report,"\t"));
            report << std::endl;
            report << "value\t";
            std::copy(data_profile.begin(),data_profile.end(),std::ostream_iterator<float>(report,"\t"));
            if(!data_ci1.empty())
            {
                report << std::endl;
                report << "CI\t";
                std::copy(data_ci1.begin(),data_ci1.end(),std::ostream_iterator<float>(report,"\t"));
            }
            if(!data_ci2.empty())
            {
                report << std::endl;
                report << "CI\t";
                std::copy(data_ci2.begin(),data_ci2.end(),std::ostream_iterator<float>(report,"\t"));
            }
            report << std::endl;
            continue;
        }

        std::string file_name_stat = file_name + "." + cmd;
        // export statistics
        if(QString(cmd.c_str()).startsWith("tdi"))
        {
            file_name_stat += ".nii.gz";
            tipl::matrix<4,4,float> tr;
            tipl::geometry<3> dim;
            tipl::vector<3,float> vs;
            tr.identity();
            dim = handle->dim;
            vs = handle->vs;
            if(!get_t1t2_nifti(handle,dim,vs,tr) && QString(cmd.c_str()).startsWith("tdi2"))
                {
                    unsigned int ratio = 4;
                    tr[0] = tr[5] = tr[10] = ratio;
                    dim = tipl::geometry<3>(handle->dim[0]*ratio,
                                            handle->dim[1]*ratio,
                                            handle->dim[2]*ratio);
                    vs /= float(ratio);
                }
            std::vector<std::shared_ptr<TractModel> > tract;
            tract.push_back(tract_model);
            std::cout << "export TDI to " << file_name_stat;
            if(QString(cmd.c_str()).endsWith("color"))
                std::cout << " in RGB color";
            if(QString(cmd.c_str()).endsWith("end"))
                std::cout << " end point only";
            std::cout << std::endl;
            TractModel::export_tdi(file_name_stat.c_str(),tract,dim,vs,tr,
                                   QString(cmd.c_str()).endsWith("color"),
                                   QString(cmd.c_str()).endsWith("end"));
            continue;
        }


        if(cmd == "stat")
        {
            file_name_stat += ".txt";
            std::cout << "export statistics to " << file_name_stat << std::endl;
            std::ofstream out_stat(file_name_stat.c_str());
            if(!out_stat)
            {
                std::cout << "Output statistics to file_name_stat failed. Please check write permission" << std::endl;
                return;
            }
            std::string result;
            tract_model->get_quantitative_info(handle,result);
            out_stat << result;
            continue;
        }

        {
            if(cmd.find('.') != std::string::npos)
                cmd = cmd.substr(0,cmd.find('.'));
            else
                file_name_stat += ".txt";
            if(handle->get_name_index(cmd) != handle->view_item.size())
            {
                tract_model->save_data_to_file(handle,file_name_stat.c_str(),cmd);
                continue;
            }
        }
        std::cout << "invalid export option:" << cmd << std::endl;
        continue;
    }
}
void save_connectivity_matrix(std::shared_ptr<fib_data> handle,
                              std::shared_ptr<TractModel> tract_model,
                              ConnectivityMatrix& data,
                              const std::string& source,
                              const std::string& connectivity_roi,
                              const std::string& connectivity_value,
                              float t,
                              bool use_end_only)
{
    std::cout << "count tracks by " << (use_end_only ? "ending":"passing") << std::endl;
    std::cout << "calculate matrix using " << connectivity_value << std::endl;
    if(!data.calculate(handle,*(tract_model.get()),connectivity_value,use_end_only,t))
    {
        std::cout << "connectivity calculation error:" << data.error_msg << std::endl;
        return;
    }
    if(data.overlap_ratio > 0.5f)
    {
        std::cout << "the ROIs have a large overlapping area (ratio: "
                  << data.overlap_ratio << "). The network measure calculated may not be reliable" << std::endl;
    }
    if(connectivity_value == "trk")
        return;
    std::string file_name_stat(source);
    file_name_stat += ".";
    file_name_stat += (QFileInfo(connectivity_roi.c_str()).exists()) ? QFileInfo(connectivity_roi.c_str()).baseName().toStdString():connectivity_roi;
    file_name_stat += ".";
    file_name_stat += connectivity_value;
    file_name_stat += use_end_only ? ".end":".pass";
    std::string network_measures(file_name_stat),connectogram(file_name_stat);
    file_name_stat += ".connectivity.mat";
    std::cout << "export connectivity matrix to " << file_name_stat << std::endl;
    data.save_to_file(file_name_stat.c_str());
    connectogram += ".connectogram.txt";
    std::cout << "export connectogram to " << connectogram << std::endl;
    data.save_to_connectogram(connectogram.c_str());

    network_measures += ".network_measures.txt";
    std::cout << "export network measures to " << network_measures << std::endl;
    std::string report;
    data.network_property(report);
    std::ofstream out(network_measures.c_str());
    out << report;
}


bool load_atlas_from_list(std::shared_ptr<fib_data> handle,
                          const std::string& atlas_name,
                          std::vector<std::shared_ptr<atlas> >& atlas_list)
{
    std::cout << "searching " << atlas_name << " from the atlas pool..." << std::endl;
    if(!atl_load_atlas(atlas_name,atlas_list))
    {
        std::cout << "ERROR: file or atlas does not exist:" << atlas_name << std::endl;
        return false;
    }
    std::cout << "loading atlas " << atlas_name << " from the atlas list." << std::endl;
    if(!handle->can_map_to_mni())
    {
        std::cout << "ERROR: no mni mapping" << std::endl;
        return false;
    }
    return true;
}
bool load_nii(std::shared_ptr<fib_data> handle,
              const std::string& file_name,
              std::vector<std::shared_ptr<ROIRegion> >& regions,
              std::vector<std::string>& names);
void get_connectivity_matrix(std::shared_ptr<fib_data> handle,
                             std::shared_ptr<TractModel> tract_model)
{
    std::string source;
    QStringList connectivity_list = QString(po.get("connectivity").c_str()).split(",");
    QStringList connectivity_type_list = QString(po.get("connectivity_type","end").c_str()).split(",");
    QStringList connectivity_value_list = QString(po.get("connectivity_value","count").c_str()).split(",");
    if(po.has("output"))
        source = po.get("output");
    if(source == "no_file" || source.empty())
        source = po.get("source");
    for(int i = 0;i < connectivity_list.size();++i)
    {
        std::string roi_file_name = connectivity_list[i].toStdString();
        std::cout << "loading " << roi_file_name << std::endl;
        ConnectivityMatrix data;
        // atlas name?
        if(!QFileInfo(roi_file_name.c_str()).exists())
        {
            std::vector<std::shared_ptr<atlas> > atlas_list;
            if(!load_atlas_from_list(handle,roi_file_name,atlas_list))
                continue;
            data.set_atlas(atlas_list[0],handle->get_mni_mapping());
        }
        else
        if(QFileInfo(roi_file_name.c_str()).suffix() == "txt") // a roi list
        {
            std::string dir = QFileInfo(roi_file_name.c_str()).absolutePath().toStdString();
            dir += "/";
            std::ifstream in(roi_file_name.c_str());
            std::string line;
            std::vector<std::shared_ptr<ROIRegion> > regions;
            while(std::getline(in,line))
            {
                std::shared_ptr<ROIRegion> region(new ROIRegion(handle.get()));
                std::string fn;
                if(QFileInfo(line.c_str()).exists())
                    fn = line;
                else
                    fn = dir + line;
                if(!region->LoadFromFile(fn.c_str()))
                {
                    std::cout << "failed to open file as a region:" << fn << std::endl;
                    return;
                }
                regions.push_back(region);
                data.region_name.push_back(QFileInfo(line.c_str()).baseName().toStdString());
            }
            data.set_regions(handle->dim,regions);
            std::cout << "a total of " << data.region_count << " regions are loaded." << std::endl;
        }
        else {
            std::vector<std::shared_ptr<ROIRegion> > regions;
            std::vector<std::string> names;
            if(load_nii(handle,roi_file_name,regions,names))
            {
                data.region_name = names;
                data.set_regions(handle->dim,regions);
            }
            else {
                std::cout << "ERROR: cannot load the ROI file:" << roi_file_name << std::endl;
                return;
            }
        }

        for(int j = 0;j < connectivity_type_list.size();++j)
        for(int k = 0;k < connectivity_value_list.size();++k)
            save_connectivity_matrix(handle,tract_model,data,source,roi_file_name,connectivity_value_list[k].toStdString(),
                                     po.get("connectivity_threshold",0.001f),
                                     connectivity_type_list[j].toLower() == QString("end"));
    }
}

// test example
// --action=trk --source=./test/20100129_F026Y_WANFANGYUN.src.gz.odf8.f3rec.de0.dti.fib.gz --method=0 --fiber_count=5000

std::shared_ptr<fib_data> cmd_load_fib(const std::string file_name)
{
    std::shared_ptr<fib_data> handle(new fib_data);
    std::cout << "loading " << file_name << "..." <<std::endl;
    if(!QFileInfo(file_name.c_str()).exists())
    {
        std::cout << file_name << " does not exist. terminating..." << std::endl;
        return std::shared_ptr<fib_data>();
    }
    if (!handle->load_from_file(file_name.c_str()))
    {
        std::cout << "open file " << file_name << " failed" << std::endl;
        std::cout << "msg:" << handle->error_msg << std::endl;
        return std::shared_ptr<fib_data>();
    }
    return handle;
}

bool load_region(std::shared_ptr<fib_data> handle,
                 ROIRegion& roi,const std::string& region_text)
{
    std::cout << "read region from " << region_text << std::endl;
    QStringList str_list = QString(region_text.c_str()).split(",");// splitting actions
    std::string file_name = str_list[0].toStdString();
    std::string region_name;

    // --roi=file_name:value
    if(file_name.find_last_of(':') != std::string::npos &&
       file_name.at(file_name.find_last_of(':')+1) != '\\')
    {
        region_name = file_name.substr(file_name.find_last_of(':')+1);
        file_name = file_name.substr(0,file_name.find_last_of(':'));
    }

    if(!QFileInfo(file_name.c_str()).exists())
    {
        if(!load_atlas_from_list(handle,file_name,handle->atlas_list))
            return false;
        if(region_name.empty())
        {
            std::cout << "ERROR: please assign region name of an atlas." << std::endl;
            return false;
        }
        const tipl::image<tipl::vector<3,float>,3 >& mapping = handle->get_mni_mapping();
        std::cout << "loading " << region_name << " from " << file_name << " atlas" << std::endl;
        tipl::vector<3> null;
        std::vector<tipl::vector<3,short> > cur_region;
        for(unsigned int i = 0;i < handle->atlas_list.size();++i)
            if(handle->atlas_list[i]->name == file_name)
                for (unsigned int label_index = 0; label_index < handle->atlas_list[i]->get_list().size(); ++label_index)
                    if(handle->atlas_list[i]->get_list()[label_index] == region_name)
                {
                    for (tipl::pixel_index<3>index(mapping.geometry());index < mapping.size();++index)
                        if(mapping[index.index()] != null && handle->atlas_list[i]->is_labeled_as(mapping[index.index()],label_index))
                            cur_region.push_back(tipl::vector<3,short>(index.begin()));
                }
        roi.add_points(cur_region,false);
    }
    else
    {
        std::vector<std::shared_ptr<ROIRegion> > regions;
        std::vector<std::string> names;
        if(!load_nii(handle,file_name,regions,names))
            return false;
        if(region_name.empty())
            roi = *(regions[0].get());
        else
        {
            bool found = false;
            for(size_t index = 0;index < names.size();++index)
                if(names[index] == region_name ||
                   names[index] == QFileInfo(file_name.c_str()).baseName().toStdString() + "_" + region_name)
                {
                    found = true;
                    roi = *(regions[index].get());
                    break;
                }
            if(!found)
            {
                std::cout << "ERROR: cannot find " << region_name << " in the NIFTI file." << std::endl;
                return false;
            }
        }
    }
    // now perform actions
    for(int i = 1;i < str_list.size();++i)
    {
        std::cout << str_list[i].toStdString() << " applied." << std::endl;
        roi.perform(str_list[i].toStdString());
    }
    if(roi.empty())
        std::cout << "WARNING: " << file_name << " is an empty region file" << std::endl;
    return true;
}

void trk_post(std::shared_ptr<fib_data> handle,std::shared_ptr<TractModel> tract_model)
{
    // save file
    if(po.has("output"))
    {
        std::string file_list = po.get("output");
        std::replace(file_list.begin(),file_list.end(),',',' ');
        std::istringstream in(file_list);
        std::string f;
        while(in >> f)
        {
            if(po.has("ref")) // save track in T1W/T2W space
            {
                std::vector<std::string> files;
                files.push_back(po.get("ref"));
                CustomSliceModel new_slice(handle.get());
                if(!new_slice.initialize(files))
                {
                    std::cout << "ERROR reading ref image file" << std::endl;
                    return;
                }
                new_slice.thread->wait();
                new_slice.update_transform();
                std::cout << "applying linear registration." << std::endl;
                std::cout << new_slice.T[0] << " " << new_slice.T[1] << " " << new_slice.T[2] << " " << new_slice.T[3] << std::endl;
                std::cout << new_slice.T[4] << " " << new_slice.T[5] << " " << new_slice.T[6] << " " << new_slice.T[7] << std::endl;
                std::cout << new_slice.T[8] << " " << new_slice.T[9] << " " << new_slice.T[10] << " " << new_slice.T[11] << std::endl;
                tract_model->save_transformed_tracts_to_file(f.c_str(),new_slice.dim,new_slice.vs,new_slice.invT,false);
            }
            else
            if(f != "no_file")
            {
                if (!tract_model->save_tracts_to_file(f.c_str()))
                    std::cout << "cannot save tracks as " << f << ". Please check write permission, directory, and disk space." << std::endl;
            }
        }
    }
    if(po.has("cluster"))
    {
        std::string cmd = po.get("cluster");
        std::replace(cmd.begin(),cmd.end(),',',' ');
        std::istringstream in(cmd);
        int method = 0,count = 0,detail = 0;
        std::string name;
        in >> method >> count >> detail >> name;
        std::cout << "cluster method: " << method << std::endl;
        std::cout << "cluster count: " << count << std::endl;
        std::cout << "cluster resolution (if method is 0) : " << detail << " mm" << std::endl;
        std::cout << "run clustering." << std::endl;
        tract_model->run_clustering(uint8_t(method),uint32_t(count),detail);
        std::ofstream out(name);
        std::cout << "cluster label saved to " << name << std::endl;
        std::copy(tract_model->get_cluster_info().begin(),tract_model->get_cluster_info().end(),std::ostream_iterator<int>(out," "));
    }

    if(po.has(("end_point")))
        tract_model->save_end_points(po.get("end_point").c_str());

    // allow adding other slices for connectivity and statistics
    check_other_slices(handle);

    if(po.has("connectivity"))
        get_connectivity_matrix(handle,tract_model);

    if(po.has("export"))
        export_track_info(handle,tract_model);
}

bool load_roi(std::shared_ptr<fib_data> handle,std::shared_ptr<RoiMgr> roi_mgr)
{
    const int total_count = 18;
    char roi_names[total_count][5] = {"roi","roi2","roi3","roi4","roi5","roa","roa2","roa3","roa4","roa5","end","end2","seed","ter","ter2","ter3","ter4","ter5"};
    unsigned char type[total_count] = {0,0,0,0,0,1,1,1,1,1,2,2,3,4,4,4,4,4};
    for(int index = 0;index < total_count;++index)
    if (po.has(roi_names[index]))
    {
        ROIRegion roi(handle.get());
        QStringList roi_list = QString(po.get(roi_names[index]).c_str()).split("+");
        for(int i= 0;i < roi_list.size();++i)
        {
            if(!load_region(handle,roi,roi_list[i].toStdString()))
                return false;
            roi_mgr->setRegions(roi.get_region_voxels_raw(),roi.resolution_ratio,type[index],roi_list[i].toStdString().c_str());
        }
    }
    if(po.has("track_id"))
    {
        std::cout << "Consider using --action=atk for automatic fiber tracking" << std::endl;
        if(!handle->load_track_atlas())
        {
            std::cout << handle->error_msg << std::endl;
            return false;
        }
        std::string name = po.get("track_id");
        size_t track_id = handle->tractography_name_list.size();
        for(size_t i = 0;i < handle->tractography_name_list.size();++i)
            if(name == handle->tractography_name_list[i])
            {
                track_id = i;
                break;
            }
        if(track_id == handle->tractography_name_list.size())
        {
            std::cout << "cannot find " << name << " in " << handle->tractography_atlas_file_name << std::endl;
            return false;
        }
        if(!roi_mgr->setAtlas(uint32_t(track_id),po.get("tolerance",16.0f)/handle->vs[0]))
        {
            std::cout << handle->error_msg << std::endl;
            return false;
        }
        std::cout << "set target track: " << handle->tractography_name_list[track_id] << std::endl;
    }
    return true;
}

int trk(std::shared_ptr<fib_data> handle);
int trk(void)
{
    try{
        std::shared_ptr<fib_data> handle = cmd_load_fib(po.get("source"));
        if(!handle.get())
            return 1;
        return trk(handle);
    }
    catch(std::exception const&  ex)
    {
        std::cout << "program terminated due to exception:" << ex.what() << std::endl;
    }
    catch(...)
    {
        std::cout << "program terminated due to unkown exception" << std::endl;
    }
    return 0;
}
int trk(std::shared_ptr<fib_data> handle)
{
    if (po.has("threshold_index"))
    {
        if(!handle->dir.set_tracking_index(po.get("threshold_index")))
        {
            std::cout << "failed...cannot find the index" << std::endl;
            return 1;
        }
    }
    if (po.has("dt_threshold_index"))
    {
        if(!handle->dir.set_dt_index(po.get("dt_threshold_index")))
        {
            std::cout << "failed...cannot find the dt index" << std::endl;
            return 1;
        }
    }

    tipl::geometry<3> geometry = handle->dim;
    const float *fa0 = handle->dir.fa[0];
    float otsu = tipl::segmentation::otsu_threshold(tipl::make_image(fa0,geometry));


    ThreadData tracking_thread(handle.get());
    tracking_thread.param.default_otsu = po.get("otsu_threshold",0.6f);
    tracking_thread.param.threshold = po.get("fa_threshold",0.0f);
    tracking_thread.param.dt_threshold = po.get("dt_threshold",0.2f);
    tracking_thread.param.cull_cos_angle = float(std::cos(po.get("turning_angle",0.0)*3.14159265358979323846/180.0));
    tracking_thread.param.step_size = po.get("step_size",0.0f);
    tracking_thread.param.smooth_fraction = po.get("smoothing",0.0f);
    tracking_thread.param.min_length = po.get("min_length",30.0f);
    tracking_thread.param.max_length = std::max<float>(tracking_thread.param.min_length,po.get("max_length",300.0f));

    tracking_thread.param.tracking_method = uint8_t(po.get("method",int(0)));
    tracking_thread.param.initial_direction  = uint8_t(po.get("initial_dir",int(0)));
    tracking_thread.param.interpolation_strategy = uint8_t(po.get("interpolation",int(0)));
    tracking_thread.param.center_seed = uint8_t(po.get("seed_plan",int(0)));
    tracking_thread.param.random_seed = uint8_t(po.get("random_seed",int(0)));
    tracking_thread.param.check_ending = uint8_t(po.get("check_ending",int(0)));
    tracking_thread.param.tip_iteration = uint8_t(po.get("tip_iteration",
                                                  (po.has("track_id") | po.has("dt_threshold_index") ) ? 16 : 0));

    if (po.has("fiber_count"))
    {
        tracking_thread.param.termination_count = po.get("fiber_count",uint32_t(tracking_thread.param.termination_count));
        tracking_thread.param.stop_by_tract = 1;
        tracking_thread.param.max_seed_count = po.get("seed_count",0);
    }
    else
    {
        if (po.has("seed_count"))
            tracking_thread.param.termination_count = po.get("seed_count",uint32_t(tracking_thread.param.termination_count));
        tracking_thread.param.stop_by_tract = 0;
    }



    QStringList cnt_file_name;
    QString cnt_type;

    if(po.has("connectometry_source"))
    {
        std::string names = po.get("connectometry_source").c_str();
        cnt_file_name = QString(names.c_str()).split(",");
        if(!po.has("connectometry_type"))
        {
            std::cout << "please assign the connectometry analysis type." << std::endl;
            return 1;
        }
        cnt_type = po.get("connectometry_type").c_str();
    }

    if(po.get("thread_count",int(std::thread::hardware_concurrency())) < 1)
    {
        std::cout << "invalid thread_count number" << std::endl;
        return 1;
    }
    if(po.has("parameter_id"))
        tracking_thread.param.set_code(po.get("parameter_id"));

    if(!load_roi(handle,tracking_thread.roi_mgr))
        return 1;

    if (tracking_thread.roi_mgr->seeds.empty())
    {
        tracking_thread.roi_mgr->setWholeBrainSeed(
                    tracking_thread.param.threshold == 0.0f ?
                        otsu*tracking_thread.param.default_otsu:tracking_thread.param.threshold);
    }
    std::shared_ptr<TractModel> tract_model(new TractModel(handle.get()));

    if(!cnt_file_name.empty())
    {
        QStringList connectometry_threshold;
        if(!po.has("connectometry_threshold"))
        {
            std::cout << "please assign the connectometry threshold." << std::endl;
            return 1;
        }
        connectometry_threshold = QString(po.get("connectometry_threshold").c_str()).split(",");
        for(int i = 0;i < cnt_file_name.size();++i)
        {
            connectometry_result cnt;
            std::cout << "loading individual file:" << cnt_file_name[i].toStdString() << std::endl;
            if(cnt_type == "iva" && !cnt.individual_vs_atlas(handle,cnt_file_name[i].toLocal8Bit().begin(),0))
            {
                std::cout << "ERROR loading connectometry file:" << cnt.error_msg <<std::endl;
                return 1;
            }
            if(cnt_type == "ivp" && !cnt.individual_vs_db(handle,cnt_file_name[i].toLocal8Bit().begin()))
            {
                std::cout << "ERROR loading connectometry file:" << cnt.error_msg <<std::endl;
                return 1;
            }
            if(cnt_type == "ivi")
            {
                std::cout << "loading individual file:" << cnt_file_name[i+1].toStdString() << std::endl;
                if(!cnt.individual_vs_individual(handle,cnt_file_name[i].toLocal8Bit().begin(),
                                                              cnt_file_name[i+1].toLocal8Bit().begin(),0))
                {
                    std::cout << "ERROR loading connectometry file:" << cnt.error_msg <<std::endl;
                    return 1;
                }
                ++i;
            }
            for(int j = 0;j < connectometry_threshold.size();++j)
            {
                double t = connectometry_threshold[j].toDouble();
                handle->dir.set_tracking_index(int(handle->dir.index_data.size()-((t > 0) ? 2:1)));
                std::cout << "mapping track with " << ((t > 0) ? "increased":"decreased") << " connectivity at " << std::fabs(t) << std::endl;
                std::cout << "start tracking." << std::endl;
                tracking_thread.param.threshold = float(std::fabs(t));
                tracking_thread.run(po.get("thread_count",uint32_t(std::thread::hardware_concurrency())),true);
                tract_model->report += tracking_thread.report.str();
                tracking_thread.fetchTracks(tract_model.get());
                std::ostringstream out;
                out << cnt_file_name[i].toStdString() << "." << cnt_type.toStdString()
                        << ((t > 0) ? "inc":"dec") << std::fabs(t) << ".tt.gz" << std::endl;
                if(!tract_model->save_tracts_to_file(out.str().c_str()))
                {
                    std::cout << "cannot save file to " << out.str()
                              << ". Please check write permission, directory, and disk space." << std::endl;
                    return 1;
                }
                std::vector<std::vector<float> > tmp;
                tract_model->release_tracts(tmp);
            }
        }
        return 0;
    }


    std::cout << "start tracking." << std::endl;
    tracking_thread.run(uint32_t(po.get("thread_count",int(std::thread::hardware_concurrency()))),true);
    tract_model->report += tracking_thread.report.str();

    tracking_thread.fetchTracks(tract_model.get());
    std::cout << "finished tracking." << std::endl;

    if(po.has("report"))
    {
        std::ofstream out(po.get("report").c_str());
        out << tract_model->report;
    }

    if(tract_model->get_visible_track_count() && po.has("refine") && (po.get("refine",1) >= 1))
    {
        for(int i = 0;i < po.get("refine",1);++i)
            tract_model->trim();
        std::cout << "refine tracking result..." << std::endl;
        std::cout << "convert tracks to seed regions" << std::endl;
        tracking_thread.roi_mgr->seeds.clear();
        std::vector<tipl::vector<3,short> > points;
        tract_model->to_voxel(points,1.0f);
        tract_model->clear();
        tracking_thread.roi_mgr->setRegions(points,1.0f,3/*seed*/,"refine seeding region");


        std::cout << "restart tracking..." << std::endl;
        tracking_thread.run(po.get("thread_count",uint32_t(std::thread::hardware_concurrency())),true);
        tracking_thread.fetchTracks(tract_model.get());
        std::cout << "finished tracking." << std::endl;

        if(tract_model->get_visible_track_count() == 0)
        {
            std::cout << "no tract generated. Terminating..." << std::endl;
            return 0;
        }
    }
    std::cout << tract_model->get_visible_track_count() << " tracts are generated using " << tracking_thread.get_total_seed_count() << " seeds."<< std::endl;
    tracking_thread.apply_tip(tract_model.get());
    std::cout << tract_model->get_deleted_track_count() << " tracts are removed by pruning." << std::endl;


    if(tract_model->get_visible_track_count() == 0)
    {
        std::cout << "no tract to process. Terminating..." << std::endl;
        return 0;
    }
    std::cout << "The final analysis results in " << tract_model->get_visible_track_count() << " tracts." << std::endl;


    if (po.has("delete_repeat"))
    {
        std::cout << "deleting repeat tracks..." << std::endl;
        float distance = po.get("delete_repeat",float(1));
        tract_model->delete_repeated(distance);
        std::cout << "repeat tracks with distance smaller than " << distance <<" voxel distance are deleted" << std::endl;
    }
    if(po.has("trim"))
    {
        std::cout << "trimming tracks..." << std::endl;
        int trim = po.get("trim",int(1));
        for(int i = 0;i < trim;++i)
            tract_model->trim();
    }

    // if no output assigned, assign a default track output file name
    if (!po.has("output"))
        po.set("output",po.get("source")+".tt.gz");

    // save track
    trk_post(handle,tract_model);
    return 0;
}
