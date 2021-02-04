#include <QApplication>
#include <QFileInfo>

#include "tracking/region/Regions.h"
#include "tracking/atlasdialog.h"
#include "connectometry/group_connectometry.hpp"
#include "ui_group_connectometry.h"
#include "program_option.hpp"
bool load_region(std::shared_ptr<fib_data> handle,
                 ROIRegion& roi,const std::string& region_text);
int cnt(void)
{
    std::shared_ptr<group_connectometry_analysis> database(new group_connectometry_analysis);
    std::cout << "reading connectometry db" <<std::endl;
    if(!database->load_database(po.get("source").c_str()))
    {
        std::cout << "invalid database format" << std::endl;
        return 1;
    }
    std::shared_ptr<group_connectometry> vbc(new group_connectometry(0,database,po.get("source").c_str(),false));
    vbc->setAttribute(Qt::WA_DeleteOnClose);
    vbc->show();
    //vbc->hide();

    if(!po.has("demo"))
    {
        std::cout << "please assign demographic file" << std::endl;
        return 1;
    }
    std::string error_msg;
    if(!vbc->load_demographic_file(po.get("demo").c_str(),error_msg))
    {
        std::cout << error_msg << std::endl;
        return 1;
    }

    if(!po.has("voi") || !po.has("variable_list"))
    {
        std::cout << "please assign --voi and --variable_list" << std::endl;
        return 1;
    }
    {
        std::string var_text = po.get("variable_list");
        std::replace(var_text.begin(),var_text.end(),',',' ');
        std::istringstream var_in(var_text);
        std::vector<int> variable_list(
                    (std::istream_iterator<int>(var_in)),
                    (std::istream_iterator<int>()));

        for(int i = 0;i < vbc->ui->variable_list->count();++i)
            vbc->ui->variable_list->item(i)->setCheckState(Qt::Unchecked);

        int voi_index = po.get("voi",0);
        int voi_sel = -1;
        std::string voi_text;
        std::cout << "variables=";
        for(int i = 0;i < variable_list.size();++i)
        {
            int index = variable_list[i];
            if(index >= vbc->ui->variable_list->count())
            {
                std::cout << "invalid number in the variable_list:" << index << std::endl;
                return 1;
            }
            if(index == voi_index)
            {
                voi_sel = vbc->ui->variable_list->count();
                voi_text = vbc->ui->variable_list->item(index)->text().toStdString();
            }
            vbc->ui->variable_list->item(index)->setCheckState(Qt::Checked);
            if(i)
                std::cout << ",";
            std::cout << vbc->ui->variable_list->item(index)->text().toStdString();
        }
        std::cout << std::endl;
        if(voi_sel == -1)
        {
            std::cout << "variable of interest is not included in the variable list" << std::endl;
            return 1;
        }
        vbc->on_variable_list_clicked(QModelIndex());
        vbc->ui->foi->update();
        vbc->ui->foi->setCurrentText(voi_text.c_str());
        std::cout << "study variable=" << vbc->ui->foi->currentText().toStdString() << std::endl;

    }

    vbc->ui->threshold->setValue(double(po.get("t_threshold",float(vbc->ui->threshold->value()))));
    vbc->ui->tip->setValue(po.get("tip",vbc->ui->tip->value()));

    vbc->ui->nonparametric->setChecked(po.get("nonparametric",1) == 1);
    vbc->ui->permutation_count->setValue(po.get("permutation",vbc->ui->permutation_count->value()));
    vbc->ui->length_threshold->setValue(po.get("length_threshold",vbc->ui->length_threshold->value()));
    vbc->ui->select_text->setText(po.get("select",vbc->ui->select_text->text().toStdString()).c_str());
    vbc->ui->tip->setValue(po.get("tip",vbc->ui->tip->value()));
    if(po.has("output"))
        vbc->ui->output_name->setText(po.get("output").c_str());

    if(po.get("normalize_qa",(vbc->vbc->handle->db.index_name == "sdf" || vbc->vbc->handle->db.index_name == "qa") ? 1:0))
        vbc->ui->normalize_qa->setChecked(true);
    else
        vbc->ui->normalize_qa->setChecked(false);

    if(po.get("exclude_cb",0))
        vbc->ui->exclude_cb->setChecked(true);
    else
        vbc->ui->exclude_cb->setChecked(false);

    if(po.has("fdr_threshold"))
    {
        vbc->ui->fdr_control->setChecked(true);
        vbc->ui->fdr_threshold->setValue(po.get("fdr_threshold",0.05f));
    }
    else
    {
        vbc->ui->fdr_control->setChecked(false);
    }

    // check rois
    {
        const int total_count = 18;
        char roi_names[total_count][5] = {"roi","roi2","roi3","roi4","roi5","roa","roa2","roa3","roa4","roa5","end","end2","seed","ter","ter2","ter3","ter4","ter5"};
        unsigned char type[total_count] = {0,0,0,0,0,1,1,1,1,1,2,2,3,4,4,4,4,4};
        for(int index = 0;index < total_count;++index)
        if (po.has(roi_names[index]))
        {
            ROIRegion roi(vbc->vbc->handle.get());
            if(!load_region(vbc->vbc->handle,roi,po.get(roi_names[index])))
                return 1;
            vbc->add_new_roi(po.get(roi_names[index]).c_str(),
                             po.get(roi_names[index]).c_str(),
                             roi.get_region_voxels_raw(),type[index]);
            vbc->ui->roi_user_defined->setChecked(true);
            vbc->ui->roi_whole_brain->setChecked(false);
        }
    }

    std::cout << "running connectometry" << std::endl;
    vbc->on_run_clicked();
    if(vbc->vbc->threads.empty())
        return 0;
    vbc->vbc->wait();
    std::cout << "output results" << std::endl;
    std::cout << vbc->vbc->report << std::endl;
    vbc->calculate_FDR();
    vbc->close();
    vbc.reset();
    return 0;
}


std::shared_ptr<fib_data> cmd_load_fib(const std::string file_name);
extern std::string fib_template_file_name_2mm;
int trk(std::shared_ptr<fib_data> handle);
int cnt_ind(void)
{
    std::shared_ptr<fib_data> handle = cmd_load_fib(po.get("source"));
    if(!handle.get())
        return 1;
    int normalization = po.get("norm",0);
    std::cout << "normalization=" << normalization << std::endl;

    if(!po.has("study"))
    {
        std::cout << "please assign the study FIB file to --study." << std::endl;
        return 1;
    }

    if(!handle->is_qsdr)
    {
        std::cout << "please assign a QSDR reconstructed FIB file to --source." << std::endl;
        return 1;
    }

    connectometry_result cnt_result;
    if(handle->has_odfs())
    {
        std::cout << "individual FIB compared with individual FIB." << std::endl;
        std::shared_ptr<fib_data> temp_handle = cmd_load_fib(po.get("template",fib_template_file_name_2mm));
        if(!temp_handle.get())
            return 1;
        if(!cnt_result.individual_vs_individual(temp_handle,po.get("source").c_str(),po.get("study").c_str(),normalization))
            goto error;
        else
            goto run;
    }

    if(handle->db.has_db())
    {
        std::cout << "connectometry db compared with individual FIB." << std::endl;
        if(!cnt_result.individual_vs_db(handle,po.get("study").c_str()))
            goto error;
        else
            goto run;
    }

    // versus template
    {
        std::cout << "individual FIB compared with a template FIB" << std::endl;
        if(normalization == 0)
            normalization = 1;
        if(!cnt_result.individual_vs_atlas(handle,po.get("study").c_str(),normalization))
            goto error;
        else
            goto run;
    }

    {
        run:
        handle->report = handle->db.report + cnt_result.report;
        trk(handle);
        return 0;
    }

    {
        error:
        std::cout << cnt_result.error_msg << std::endl;
        return 1;
    }
}
