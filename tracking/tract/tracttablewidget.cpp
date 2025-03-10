#include <QFileDialog>
#include <QMessageBox>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QColorDialog>
#include <QInputDialog>
#include "tracttablewidget.h"
#include "tracking/tracking_window.h"
#include "libs/tracking/tract_model.hpp"
#include "libs/tracking/tracking_thread.hpp"
#include "opengl/glwidget.h"
#include "../region/regiontablewidget.h"
#include "ui_tracking_window.h"
#include "opengl/renderingtablewidget.h"
#include "libs/gzip_interface.hpp"
#include "atlas.hpp"
#include "../color_bar_dialog.hpp"
#include "gzip_interface.hpp"

void show_info_dialog(const std::string& title,const std::string& result);

TractTableWidget::TractTableWidget(tracking_window& cur_tracking_window_,QWidget *parent) :
    QTableWidget(parent),cur_tracking_window(cur_tracking_window_)
{
    setColumnCount(4);
    setColumnWidth(0,200);
    setColumnWidth(1,50);
    setColumnWidth(2,50);
    setColumnWidth(3,50);

    QStringList header;
    header << "Name" << "Tracts" << "Deleted" << "Seeds";
    setHorizontalHeaderLabels(header);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setAlternatingRowColors(true);
    setStyleSheet("QTableView {selection-background-color: #AAAAFF; selection-color: #000000;}");

    QObject::connect(this,SIGNAL(cellClicked(int,int)),this,SLOT(check_check_status(int,int)));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(fetch_tracts()));
}

TractTableWidget::~TractTableWidget(void)
{
}

void TractTableWidget::contextMenuEvent ( QContextMenuEvent * event )
{
    if(event->reason() == QContextMenuEvent::Mouse)
        cur_tracking_window.ui->menuTracts->popup(event->globalPos());
}

void TractTableWidget::check_check_status(int row, int col)
{
    if(col != 0)
        return;
    setCurrentCell(row,col);
    if (item(row,0)->checkState() == Qt::Checked)
    {
        if (item(row,0)->data(Qt::ForegroundRole) == QBrush(Qt::gray))
        {
            item(row,0)->setData(Qt::ForegroundRole,QBrush(Qt::black));
            emit need_update();
        }
    }
    else
    {
        if (item(row,0)->data(Qt::ForegroundRole) != QBrush(Qt::gray))
        {
            item(row,0)->setData(Qt::ForegroundRole,QBrush(Qt::gray));
            emit need_update();
        }
    }
}

void TractTableWidget::draw_tracts(unsigned char dim,int pos,
                                   QImage& scaled_image,float display_ratio,unsigned int max_count)
{
    auto selected_tracts = get_checked_tracks();
    if(selected_tracts.empty())
        return;


    unsigned int thread_count = std::thread::hardware_concurrency();
    std::vector<std::vector<std::vector<tipl::vector<2,float> > > > lines_threaded(thread_count);
    std::vector<std::vector<unsigned int> > colors_threaded(thread_count);
    auto iT = cur_tracking_window.current_slice->invT;
    max_count /= selected_tracts.size();

    tipl::par_for(selected_tracts.size(),[&](unsigned int index,unsigned int thread)
    {
        if(cur_tracking_window.slice_need_update)
            return;
        if(cur_tracking_window.current_slice->is_diffusion_space)
            selected_tracts[index]->get_in_slice_tracts(dim,pos,nullptr,lines_threaded[thread],colors_threaded[thread],max_count,
                            cur_tracking_window.slice_need_update);
        else
            selected_tracts[index]->get_in_slice_tracts(dim,pos,&iT,lines_threaded[thread],colors_threaded[thread],max_count,
                            cur_tracking_window.slice_need_update);
    });
    if(cur_tracking_window.slice_need_update)
        return;

    std::vector<std::vector<tipl::vector<2,float> > > lines(std::move(lines_threaded[0]));
    std::vector<unsigned int> colors(std::move(colors_threaded[0]));
    for(unsigned int i = 1;i < thread_count;++i)
    {
        lines.insert(lines.end(),std::make_move_iterator(lines_threaded[i].begin()),std::make_move_iterator(lines_threaded[i].end()));
        colors.insert(colors.end(),std::make_move_iterator(colors_threaded[i].begin()),std::make_move_iterator(colors_threaded[i].end()));
    }

    struct draw_point_class{
        int height;
        int width;
        std::vector<QRgb*> I;
        draw_point_class(QImage& scaled_image):I(uint32_t(scaled_image.height()))
        {
            for (int y = 0; y < scaled_image.height(); ++y)
                I[uint32_t(y)] = reinterpret_cast<QRgb*>(scaled_image.scanLine(y));
            height = scaled_image.height();
            width = scaled_image.width();
        }
        inline void operator()(int x,int y,unsigned int color)
        {
            if(y < 0 || x < 0 || y >= height || x >= width)
                return;
            I[uint32_t(y)][uint32_t(x)] = color;
        }
    } draw_point(scaled_image);


    auto draw_line = [&](int x,int y,int x1,int y1,unsigned int color)
    {
        tipl::draw_line(x,y,x1,y1,[&](int xx,int yy)
        {
            draw_point(xx,yy,color);
        });
    };

    tipl::par_for(lines.size(),[&](unsigned int i)
    {
        auto& line = lines[i];
        tipl::add_constant(line,0.5f);
        tipl::multiply_constant(line,display_ratio);
        if(line.size() >= 2)
        {
            for(size_t j = 1;j < line.size();++j)
                draw_line(int(line[j-1][0]),int(line[j-1][1]),int(line[j][0]),int(line[j][1]),colors[i]);
        }
    });
}
void TractTableWidget::addNewTracts(QString tract_name,bool checked)
{
    thread_data.push_back(std::make_shared<ThreadData>(cur_tracking_window.handle));
    tract_models.push_back(std::make_shared<TractModel>(cur_tracking_window.handle));
    insertRow(tract_models.size()-1);
    QTableWidgetItem *item0 = new QTableWidgetItem(tract_name);
    item0->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    item0->setData(Qt::ForegroundRole,checked ? QBrush(Qt::black) : QBrush(Qt::gray));
    setItem(tract_models.size()-1, 0, item0);
    for(unsigned int index = 1;index <= 3;++index)
    {
        QTableWidgetItem *item1 = new QTableWidgetItem(QString::number(0));
        item1->setFlags(item1->flags() & ~Qt::ItemIsEditable);
        setItem(tract_models.size()-1, index, item1);
    }

    setRowHeight(tract_models.size()-1,22);
    setCurrentCell(tract_models.size()-1,0);
}
void TractTableWidget::addConnectometryResults(std::vector<std::vector<std::vector<float> > >& greater,
                             std::vector<std::vector<std::vector<float> > >& lesser)
{
    for(unsigned int index = 0;index < lesser.size();++index)
    {
        if(lesser[index].empty())
            continue;
        int color = std::min<int>((index+1)*50,255);
        addNewTracts(QString("Lesser_") + QString::number((index+1)*10),false);
        tract_models.back()->add_tracts(lesser[index],tipl::rgb(255,255-color,255-color));
        item(tract_models.size()-1,1)->setText(QString::number(tract_models.back()->get_visible_track_count()));
    }
    for(unsigned int index = 0;index < greater.size();++index)
    {
        if(greater[index].empty())
            continue;
        int color = std::min<int>((index+1)*50,255);
        addNewTracts(QString("Greater_") + QString::number((index+1)*10),false);
        tract_models.back()->add_tracts(greater[index],tipl::rgb(255-color,255-color,255));
        item(tract_models.size()-1,1)->setText(QString::number(tract_models.back()->get_visible_track_count()));
    }
    cur_tracking_window.set_data("tract_color_style",1);//manual assigned
    emit need_update();
}

void TractTableWidget::start_tracking(void)
{
    bool auto_track = cur_tracking_window.ui->target->currentIndex() > 0;
    bool dT = cur_tracking_window.handle->dir.is_dt();
    if(auto_track)
    {
        if(dT)
        {
            QMessageBox::critical(this,"Error","Differential tractography cannot be comabined with automated tractography. Use regions instead");
            return;
        }
        addNewTracts(cur_tracking_window.ui->target->currentText());
    }
    else
    {
        if(dT)
            addNewTracts(QString(cur_tracking_window.handle->dir.get_dt_threshold_name().c_str())+"_"+
                         QString::number(cur_tracking_window["dt_threshold"].toDouble()));
        else
            addNewTracts(cur_tracking_window.regionWidget->getROIname());
    }
    cur_tracking_window.set_tracking_param(*thread_data.back());
    cur_tracking_window.regionWidget->setROIs(thread_data.back().get());
    thread_data.back()->run(cur_tracking_window.ui->thread_count->value(),false);
    tract_models.back()->report = cur_tracking_window.handle->report + thread_data.back()->report.str();
    show_report();
    timer->start(1000);
}

void TractTableWidget::show_report(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() == -1)
        return;
    cur_tracking_window.report(tract_models[uint32_t(currentRow())]->report.c_str());
}

void TractTableWidget::filter_by_roi(void)
{
    ThreadData track_thread(cur_tracking_window.handle);
    cur_tracking_window.set_tracking_param(track_thread);
    cur_tracking_window.regionWidget->setROIs(&track_thread);
    for(int index = 0;index < tract_models.size();++index)
    if(item(int(index),0)->checkState() == Qt::Checked)
    {
        tract_models[index]->filter_by_roi(track_thread.roi_mgr);
        item(int(index),1)->setText(QString::number(tract_models[index]->get_visible_track_count()));
        item(int(index),2)->setText(QString::number(tract_models[index]->get_deleted_track_count()));
    }
    emit need_update();
}

void TractTableWidget::fetch_tracts(void)
{
    bool has_tracts = false;
    bool has_thread = false;
    for(unsigned int index = 0;index < thread_data.size();++index)
        if(thread_data[index].get())
        {
            has_thread = true;
            // 2 for seed number
            if(thread_data[index]->fetchTracks(tract_models[index].get()))
            {
                // 1 for tract number
                item(int(index),1)->setText(
                        QString::number(tract_models[index]->get_visible_track_count()));
                has_tracts = true;
            }
            item(int(index),3)->setText(
                QString::number(thread_data[index]->get_total_seed_count()));
            if(thread_data[index]->is_ended())
            {
                has_tracts = thread_data[index]->fetchTracks(tract_models[index].get()) ||
                             thread_data[index]->fetchTracks(tract_models[index].get()); // clear both front and back buffer
                thread_data[index]->apply_tip(tract_models[index].get());
                item(int(index),1)->setText(QString::number(tract_models[index]->get_visible_track_count()));
                item(int(index),2)->setText(QString::number(tract_models[index]->get_deleted_track_count()));
                thread_data[index].reset();
            }
        }
    if(has_tracts)
        emit need_update();
    if(!has_thread)
        timer->stop();
}

void TractTableWidget::stop_tracking(void)
{
    timer->stop();
    for(unsigned int index = 0;index < thread_data.size();++index)
        thread_data[index].reset();
}
void TractTableWidget::load_tracts(QStringList filenames)
{
    if(filenames.empty())
        return;
    for(unsigned int index = 0;index < filenames.size();++index)
    {
        QString filename = filenames[index];
        if(!filename.size())
            continue;
        QString label = QFileInfo(filename).fileName();
        label.remove(".tt.gz");
        label.remove(".trk.gz");
        label.remove(".txt");
        int pos = label.indexOf(".fib.gz");
        if(pos != -1)
            label = label.right(label.length()-pos-8);
        std::string sfilename = filename.toStdString();
        addNewTracts(label);
        if(!tract_models.back()->load_from_file(&*sfilename.begin(),false))
        {
            QMessageBox::information(this,"Error",
                                     QString("Fail to load tracks from %1. \
                                Please check file access privelige or move file to other location.").arg(QFileInfo(filename).baseName()));
            continue;
        }
        if(tract_models.back()->get_cluster_info().empty()) // not multiple cluster file
        {
            item(tract_models.size()-1,1)->setText(QString::number(tract_models.back()->get_visible_track_count()));
        }
        else
        {
            std::vector<unsigned int> labels;
            labels.swap(tract_models.back()->get_cluster_info());
            load_cluster_label(labels);
            if(QFileInfo(filename+".txt").exists())
                load_tract_label(filename+".txt");
        }
    }
    emit need_update();
}

void TractTableWidget::load_tracts(void)
{
    load_tracts(QFileDialog::getOpenFileNames(
            this,"Load tracts as",QFileInfo(cur_tracking_window.windowTitle()).absolutePath(),
            "Tract files (*tt.gz *.trk *trk.gz *.tck);;Text files (*.txt);;All files (*)"));
    show_report();
}
void TractTableWidget::load_tract_label(void)
{
    QString filename = QFileDialog::getOpenFileName(
                this,"Load tracts as",QFileInfo(cur_tracking_window.windowTitle()).absolutePath(),
                "Tract files (*.txt);;All files (*)");
    if(filename.isEmpty())
        return;
    load_tract_label(filename);
}
void TractTableWidget::load_tract_label(QString filename)
{
    std::ifstream in(filename.toStdString().c_str());
    std::string line;
    for(int i = 0;in >> line && i < rowCount();++i)
        item(i,0)->setText(line.c_str());
}

void TractTableWidget::check_all(void)
{
    for(int row = 0;row < rowCount();++row)
    {
        item(row,0)->setCheckState(Qt::Checked);
        item(row,0)->setData(Qt::ForegroundRole,QBrush(Qt::black));
    }
    emit need_update();
}

void TractTableWidget::uncheck_all(void)
{
    for(int row = 0;row < rowCount();++row)
    {
        item(row,0)->setCheckState(Qt::Unchecked);
        item(row,0)->setData(Qt::ForegroundRole,QBrush(Qt::gray));
    }
    emit need_update();
}
QString TractTableWidget::output_format(void)
{
    switch(cur_tracking_window["track_format"].toInt())
    {
    case 0:
        return ".tt.gz";
    case 1:
        return ".trk.gz";
    case 2:
        return ".txt";
    }
    return "";
}

void TractTableWidget::save_all_tracts_to_dir(void)
{
    if (tract_models.empty())
        return;
    QString dir = QFileDialog::getExistingDirectory(this,"Open directory","");
    if(dir.isEmpty())
        return;
    command("save_all_tracts_to_dir",dir);
    if(!progress::aborted())
        QMessageBox::information(this,"DSI Studio","file saved");
}
void TractTableWidget::save_all_tracts_as(void)
{
    if(tract_models.empty())
        return;
    QString filename;
    filename = QFileDialog::getSaveFileName(
                this,"Save tracts as",item(currentRow(),0)->text().replace(':','_') + output_format(),
                "Tract files (*.tt.gz *tt.gz *trk.gz *.trk);;NIFTI File (*nii.gz);;Text File (*.txt);;MAT files (*.mat);;All files (*)");
    if(filename.isEmpty())
        return;
    if(command("save_tracks",filename))
        QMessageBox::information(this,"DSI Studio","file saved");
}

void TractTableWidget::set_color(void)
{
    if(tract_models.empty())
        return;
    QColor color = QColorDialog::getColor(Qt::red,(QWidget*)this,"Select color",QColorDialog::ShowAlphaChannel);
    if(!color.isValid())
        return;
    tract_models[uint32_t(currentRow())]->set_color(color.rgb());
    cur_tracking_window.set_data("tract_color_style",1);//manual assigned
    emit need_update();
}
void TractTableWidget::assign_colors(void)
{
    for(unsigned int index = 0;index < tract_models.size();++index)
    {
        tipl::rgb c;
        c.from_hsl((color_gen*1.1-std::floor(color_gen*1.1/6)*6)*3.14159265358979323846/3.0,0.85,0.7);
        color_gen++;
        tract_models[index]->set_color(c.color);
    }
    cur_tracking_window.set_data("tract_color_style",1);//manual assigned
    emit need_update();
}
void TractTableWidget::load_cluster_label(const std::vector<unsigned int>& labels,QStringList Names)
{
    std::string report = tract_models[uint32_t(currentRow())]->report;
    std::vector<std::vector<float> > tracts;
    tract_models[uint32_t(currentRow())]->release_tracts(tracts);
    delete_row(currentRow());
    unsigned int cluster_count = uint32_t(Names.empty() ? int(1+tipl::max_value(labels)):int(Names.count()));
    for(unsigned int cluster_index = 0;cluster_index < cluster_count;++cluster_index)
    {
        unsigned int fiber_num = uint32_t(std::count(labels.begin(),labels.end(),cluster_index));
        if(!fiber_num)
            continue;
        std::vector<std::vector<float> > add_tracts(fiber_num);
        for(unsigned int index = 0,i = 0;index < labels.size();++index)
            if(labels[index] == cluster_index)
            {
                add_tracts[i].swap(tracts[index]);
                ++i;
            }
        if(int(cluster_index) < Names.size())
            addNewTracts(Names[int(cluster_index)],false);
        else
            addNewTracts(QString("cluster")+QString::number(cluster_index),false);
        tract_models.back()->add_tracts(add_tracts);
        tract_models.back()->report = report;
        item(int(tract_models.size())-1,1)->setText(QString::number(tract_models.back()->get_visible_track_count()));
    }
}

void TractTableWidget::open_cluster_label(void)
{
    if(tract_models.empty())
        return;
    QString filename = QFileDialog::getOpenFileName(
            this,"Load cluster label",QFileInfo(cur_tracking_window.windowTitle()).absolutePath(),
            "Cluster label files (*.txt);;All files (*)");
    if(!filename.size())
        return;

    std::ifstream in(filename.toLocal8Bit().begin());
    std::vector<unsigned int> labels(tract_models[uint32_t(currentRow())]->get_visible_track_count());
    std::copy(std::istream_iterator<unsigned int>(in),
              std::istream_iterator<unsigned int>(),labels.begin());
    load_cluster_label(labels);
    assign_colors();
}
void TractTableWidget::auto_recognition(void)
{
    if(!cur_tracking_window.handle->load_track_atlas())
    {
        QMessageBox::information(this,"Error",cur_tracking_window.handle->error_msg.c_str());
        return;
    }
    std::vector<unsigned int> c;
    cur_tracking_window.handle->recognize(tract_models[uint32_t(currentRow())],c,
                    cur_tracking_window["autotrack_tolerance"].toFloat());
    QStringList Names;
    for(int i = 0;i < cur_tracking_window.handle->tractography_name_list.size();++i)
        Names << cur_tracking_window.handle->tractography_name_list[i].c_str();
    load_cluster_label(c,Names);
}

void TractTableWidget::recognize_rename(void)
{
    if(!cur_tracking_window.handle->load_track_atlas())
    {
        QMessageBox::information(this,"Error",cur_tracking_window.handle->error_msg.c_str());
        return;
    }
    progress prog_("Recognize and rename");
    for(unsigned int index = 0;progress::at(index,tract_models.size());++index)
        if(item(int(index),0)->checkState() == Qt::Checked)
        {
            std::map<float,std::string,std::greater<float> > sorted_list;
            if(!cur_tracking_window.handle->recognize(tract_models[index],sorted_list,true))
                return;
            item(int(index),0)->setText(sorted_list.begin()->second.c_str());
        }
}

void TractTableWidget::clustering(int method_id)
{
    if(tract_models.empty())
        return;
    bool ok = false;
    int n = QInputDialog::getInt(this,
            "DSI Studio",
            "Assign the maximum number of groups",50,1,5000,10,&ok);
    if(!ok)
        return;
    ok = true;
    double detail = method_id ? 0.0 : QInputDialog::getDouble(this,
            "DSI Studio","Clustering detail (mm):",cur_tracking_window.handle->vs[0],0.2,50.0,2,&ok);
    if(!ok)
        return;
    tract_models[uint32_t(currentRow())]->run_clustering(method_id,n,detail);
    std::vector<unsigned int> c = tract_models[uint32_t(currentRow())]->get_cluster_info();
    load_cluster_label(c);
    assign_colors();
}

void TractTableWidget::save_tracts_as(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() < 0)
        return;
    QString filename;
    filename = QFileDialog::getSaveFileName(
                this,"Save tracts as",item(currentRow(),0)->text().replace(':','_') + output_format(),
                 "Tract files (*.tt.gz *tt.gz *trk.gz *.trk);;Text File (*.txt);;MAT files (*.mat);;TCK file (*.tck);;ROI files (*.nii *nii.gz);;All files (*)");
    if(filename.isEmpty())
        return;
    std::string sfilename = filename.toLocal8Bit().begin();
    if(tract_models[uint32_t(currentRow())]->save_tracts_to_file(&*sfilename.begin()))
        QMessageBox::information(this,"DSI Studio","file saved");
    else
        QMessageBox::critical(this,"Error","Cannot write to file. Please check write permission.");
}

void TractTableWidget::save_tracts_in_native(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() < 0)
        return;
    if(!cur_tracking_window.handle->is_qsdr)
    {
        QMessageBox::information(this,"DSI Studio","This function only works with QSDR reconstructed FIB files.");
        return;
    }
    if(cur_tracking_window.handle->get_native_position().empty())
    {
        QMessageBox::information(this,"DSI Studio","No mapping information included. Please reconstruct QSDR with 'mapping' included in the output.",0);
        return;
    }

    QString filename;
    filename = QFileDialog::getSaveFileName(
                this,"Save tracts as",item(currentRow(),0)->text().replace(':','_') + output_format(),
                 "Tract files (*.tt.gz *tt.gz *trk.gz *.trk);;Text File (*.txt);;MAT files (*.mat);;All files (*)");
    if(filename.isEmpty())
        return;
    if(tract_models[uint32_t(currentRow())]->save_tracts_in_native_space(cur_tracking_window.handle,filename.toStdString().c_str()))
        QMessageBox::information(this,"DSI Studio","file saved");
    else
        QMessageBox::critical(this,"Error","Cannot write to file. Please check write permission.");
}

void TractTableWidget::save_vrml_as(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() < 0)
        return;
    QString filename;
    filename = QFileDialog::getSaveFileName(
                this,"Save tracts as",item(currentRow(),0)->text().replace(':','_') + ".obj",
                 "3D files (*.obj);;All files (*)");
    if(filename.isEmpty())
        return;
    std::string surface_text;
    /*
    if(cur_tracking_window.glWidget->surface.get() && cur_tracking_window["show_surface"].toInt())
    {
        std::ostringstream out;
        QString Coordinate, CoordinateIndex;
        const auto& point_list = cur_tracking_window.glWidget->surface->get()->point_list;
        for(unsigned int index = 0;index < point_list.size();++index)
            Coordinate += QString("%1 %2 %3 ").arg(point_list[index][0]).arg(point_list[index][1]).arg(point_list[index][2]);
        const auto& tri_list = cur_tracking_window.glWidget->surface->get()->tri_list;
        for (unsigned int j = 0;j < tri_list.size();++j)
            CoordinateIndex += QString("%1 %2 %3 -1 ").arg(tri_list[j][0]).arg(tri_list[j][1]).arg(tri_list[j][2]);
        surface_text = out.str();
    }
    */
    std::string sfilename = filename.toLocal8Bit().begin();

    tract_models[uint32_t(currentRow())]->save_vrml(&*sfilename.begin(),
                                                cur_tracking_window["tract_style"].toInt(),
                                                cur_tracking_window["tract_color_style"].toInt(),
                                                cur_tracking_window["tube_diameter"].toFloat(),
                                                cur_tracking_window["tract_tube_detail"].toInt(),surface_text);
}
void TractTableWidget::save_all_tracts_end_point_as(void)
{
    auto selected_tracts = get_checked_tracks();
    if(selected_tracts.empty())
        return;
    QString filename;
    filename = QFileDialog::getSaveFileName(
                this,
                "Save end points as",item(currentRow(),0)->text().replace(':','_') + "endpoint.nii.gz",
                "NIFTI files (*nii.gz);;All files (*)");
    if(filename.isEmpty())
        return;
    bool ok;
    float dis = float(QInputDialog::getDouble(this,
        "DSI Studio","Assign end segment length in voxel distance:",3.0,0.0,10.0,1,&ok));
    if (!ok)
        return;
    TractModel::export_end_pdi(filename.toStdString().c_str(),selected_tracts,dis);
}
void TractTableWidget::save_end_point_as(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() < 0)
        return;
    QString filename;
    filename = QFileDialog::getSaveFileName(
                this,
                "Save end points as",item(currentRow(),0)->text().replace(':','_') + "endpoint.txt",
                "Tract files (*.txt);;MAT files (*.mat);;All files (*)");
    if(filename.isEmpty())
        return;
    tract_models[uint32_t(currentRow())]->save_end_points(filename.toStdString().c_str());
}

void TractTableWidget::save_end_point_in_mni(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() < 0 || !cur_tracking_window.map_to_mni())
        return;
    QString filename;
    filename = QFileDialog::getSaveFileName(
                this,
                "Save end points as",item(currentRow(),0)->text().replace(':','_') + "endpoint.txt",
                "Tract files (*.txt);;MAT files (*.mat);;All files (*)");
    if(filename.isEmpty())
        return;

    float resolution_ratio = 2.0f;
    std::vector<tipl::vector<3,short> > points1,points2;
    tract_models[size_t(currentRow())]->to_end_point_voxels(points1,points2,resolution_ratio);
    points1.insert(points1.end(),points2.begin(),points2.end());

    std::vector<tipl::vector<3> > points(points1.begin(),points1.end());
    std::vector<float> buffer;
    for(unsigned int index = 0;index < points.size();++index)
    {
        points[index] /= resolution_ratio;
        cur_tracking_window.handle->sub2mni(points[index]);
        buffer.push_back(points1[index][0]);
        buffer.push_back(points1[index][1]);
        buffer.push_back(points1[index][2]);
    }

    if (QFileInfo(filename).suffix().toLower() == "txt")
    {
        std::ofstream out(filename.toLocal8Bit().begin(),std::ios::out);
        if (!out)
            return;
        std::copy(buffer.begin(),buffer.end(),std::ostream_iterator<float>(out," "));
    }
    if (QFileInfo(filename).suffix().toLower() == "mat")
    {
        tipl::io::mat_write out(filename.toLocal8Bit().begin());
        if(!out)
            return;
        out.write("end_points",buffer,3);
    }
}


void TractTableWidget::save_transformed_tracts(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() == -1)
        return;
    QString filename;
    filename = QFileDialog::getSaveFileName(
                this,
                "Save tracts as",item(currentRow(),0)->text() + "_in_" +
                cur_tracking_window.current_slice->get_name().c_str()+output_format(),
                 "Tract files (*.tt.gz *tt.gz *trk.gz *.trk);;Text File (*.txt);;MAT files (*.mat);;NIFTI files (*.nii *nii.gz);;All files (*)");
    if(filename.isEmpty())
        return;
    CustomSliceModel* slice = dynamic_cast<CustomSliceModel*>(cur_tracking_window.current_slice.get());
    if(!slice)
    {
        QMessageBox::critical(this,"Error","Current slice is in the DWI space. Please use regular tract saving function");
        return;
    }

    slice->update_transform();
    if(tract_models[uint32_t(currentRow())]->save_transformed_tracts_to_file(filename.toStdString().c_str(),slice->dim,slice->vs,slice->invT,false))
        QMessageBox::information(this,"DSI Studio","File saved");
    else
        QMessageBox::critical(this,"Error","File not saved. Please check write permission");
}


void TractTableWidget::save_transformed_endpoints(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() == -1)
        return;
    QString filename;
    filename = QFileDialog::getSaveFileName(
                this,
                "Save end_point as",item(currentRow(),0)->text() + "_endpoints_in_" +
                cur_tracking_window.current_slice->get_name().c_str()+
                ".txt",
                "Tract files (*.txt);;MAT files (*.mat);;All files (*)");
    if(filename.isEmpty())
        return;
    CustomSliceModel* slice = dynamic_cast<CustomSliceModel*>(cur_tracking_window.current_slice.get());
    if(!slice)
    {
        QMessageBox::critical(this,"Error","Current slice is in the DWI space. Please use regular tract saving function");
        return;
    }
    if(tract_models[uint32_t(currentRow())]->save_transformed_tracts_to_file(filename.toStdString().c_str(),slice->dim,slice->vs,slice->invT,true))
        QMessageBox::information(this,"DSI Studio","File saved");
    else
        QMessageBox::critical(this,"Error","File not saved. Please check write permission");
}
extern std::vector<std::string> fa_template_list;
void TractTableWidget::save_tracts_in_template(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() == -1 || !cur_tracking_window.map_to_mni())
        return;
    QString filename;
    filename = QFileDialog::getSaveFileName(
                this,
                "Save tracts as",item(currentRow(),0)->text() + "_in_" +
                QFileInfo(fa_template_list[cur_tracking_window.handle->template_id].c_str()).baseName() +
                output_format(),
                 "Tract files (*.tt.gz *tt.gz *trk.gz *.trk);;Text File (*.txt);;MAT files (*.mat);;NIFTI files (*.nii *nii.gz);;All files (*)");
    if(filename.isEmpty())
        return;
    if(tract_models[uint32_t(currentRow())]->save_tracts_in_template_space(cur_tracking_window.handle,filename.toStdString().c_str()))
        QMessageBox::information(this,"DSI Studio","File saved");
    else
        QMessageBox::critical(this,"Error","File not saved. Please check write permission");
}

void TractTableWidget::save_tracts_in_mni(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() == -1 || !cur_tracking_window.map_to_mni())
        return;
    QString filename;
    filename = QFileDialog::getSaveFileName(
                this,
                "Save tracts as",item(currentRow(),0)->text() + "_in_mni" + output_format(),
                 "NIFTI files (*.nii *nii.gz);;All files (*)");
    if(filename.isEmpty())
        return;
    if(tract_models[uint32_t(currentRow())]->save_tracts_in_template_space(cur_tracking_window.handle,filename.toStdString().c_str()))
        QMessageBox::information(this,"DSI Studio","File saved");
    else
        QMessageBox::critical(this,"Error","File not saved. Please check write permission");
}

void paint_track_on_volume(tipl::image<3,unsigned char>& track_map,const std::vector<float>& tracks)
{
    for(size_t j = 0;j < tracks.size();j += 3)
    {
        tipl::pixel_index<3> p(std::round(tracks[j]),std::round(tracks[j+1]),std::round(tracks[j+2]),track_map.shape());
        if(track_map.shape().is_valid(p))
            track_map[p.index()] = 1;
        if(j)
        {
            for(float r = 0.2f;r < 1.0f;r += 0.2f)
            {
                tipl::pixel_index<3> p2(std::round(tracks[j]*r+tracks[j-3]*(1-r)),
                                         std::round(tracks[j+1]*r+tracks[j-2]*(1-r)),
                                         std::round(tracks[j+2]*r+tracks[j-1]*(1-r)),track_map.shape());
                if(track_map.shape().is_valid(p2))
                    track_map[p2.index()] = 1;
            }
        }
    }
}

void TractTableWidget::deep_learning_train(void)
{
    QString filename;
    filename = QFileDialog::getSaveFileName(
                this,
                "Save network as","network.txt",
                "Text files (*.txt);;All files (*)");
    if(filename.isEmpty())
        return;
    // save atlas as a nifti file
    if(cur_tracking_window.handle->is_qsdr) //QSDR condition
    {
        tipl::image<4,int> atlas(tipl::shape<4>(
                cur_tracking_window.handle->dim[0],
                cur_tracking_window.handle->dim[1],
                cur_tracking_window.handle->dim[2],
                rowCount()));

        for(unsigned int index = 0;progress::at(index,rowCount());++index)
        {
            tipl::image<3,unsigned char> track_map(cur_tracking_window.handle->dim);
            for(unsigned int i = 0;i < tract_models[index]->get_tracts().size();++i)
                paint_track_on_volume(track_map,tract_models[index]->get_tracts()[i]);
            while(tipl::morphology::smoothing_fill(track_map))
                ;
            tipl::morphology::defragment(track_map);

            QString track_file_name = QFileInfo(filename).absolutePath() + "/" + item(int(index),0)->text() + ".nii.gz";
            gz_nifti nifti2;
            nifti2.set_voxel_size(cur_tracking_window.current_slice->vs);
            nifti2.set_image_transformation(cur_tracking_window.handle->trans_to_mni);
            nifti2 << track_map;
            nifti2.save_to_file(track_file_name.toLocal8Bit().begin());

            std::copy(track_map.begin(),track_map.end(),atlas.begin() + index*track_map.size());

            if(index+1 == rowCount())
            {
                filename = QFileInfo(filename).absolutePath() + "/tracks.nii.gz";
                gz_nifti nifti;
                nifti.set_voxel_size(cur_tracking_window.current_slice->vs);
                nifti.set_image_transformation(cur_tracking_window.handle->trans_to_mni);
                nifti << atlas;
                nifti.save_to_file(filename.toLocal8Bit().begin());
            }
        }
    }

}

void TractTableWidget::recog_tracks(void)
{
    if(currentRow() >= int(tract_models.size()) || tract_models[uint32_t(currentRow())]->get_tracts().size() == 0)
        return;
    if(!cur_tracking_window.handle->load_track_atlas())
    {
        QMessageBox::information(this,"Error",cur_tracking_window.handle->error_msg.c_str());
        return;
    }
    std::map<float,std::string,std::greater<float> > sorted_list;
    if(!cur_tracking_window.handle->recognize(tract_models[uint32_t(currentRow())],sorted_list,false))
    {
        QMessageBox::information(this,"Error","Cannot recognize tracks.");
        return;
    }
    std::ostringstream out;
    auto beg = sorted_list.begin();
    for(size_t i = 0;i < sorted_list.size();++i,++beg)
        if(beg->first != 0.0f)
            out << beg->first*100.0f << "% " << beg->second << std::endl;
    show_info_dialog("Tract Recognition Result",out.str());
}


void TractTableWidget::load_tracts_color(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() == -1)
        return;
    QString filename = QFileDialog::getOpenFileName(
            this,"Load tracts color",QFileInfo(cur_tracking_window.windowTitle()).absolutePath(),
            "Color files (*.txt);;All files (*)");
    if(filename.isEmpty())
        return;
    command("load_track_color",filename,"");
}

void TractTableWidget::load_tracts_value(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() == -1)
        return;
    QString filename = QFileDialog::getOpenFileName(
            this,"Load tracts color",QFileInfo(cur_tracking_window.windowTitle()).absolutePath(),
            "Color files (*.txt);;All files (*)");
    if(filename.isEmpty())
        return;
    std::ifstream in(filename.toStdString().c_str());
    if (!in)
        return;
    std::vector<float> values;
    std::copy(std::istream_iterator<float>(in),
              std::istream_iterator<float>(),
              std::back_inserter(values));
    if(tract_models[uint32_t(currentRow())]->get_visible_track_count() != values.size())
    {
        QMessageBox::information(this,"Inconsistent track number",
                                 QString("The text file has %1 values, but there are %2 tracks.").
                                 arg(values.size()).arg(tract_models[uint32_t(currentRow())]->get_visible_track_count()),0);
        return;
    }
    color_bar_dialog dialog(nullptr);
    auto min_max = std::minmax_element(values.begin(),values.end());
    dialog.set_value(*min_max.first,*min_max.second);
    dialog.exec();
    std::vector<unsigned int> colors(values.size());
    for(int i = 0;i < values.size();++i)
        colors[i] = (unsigned int)dialog.get_rgb(values[i]);
    tract_models[uint32_t(currentRow())]->set_tract_color(colors);
    cur_tracking_window.set_data("tract_color_style",1);//manual assigned
    emit need_update();
}

void TractTableWidget::save_tracts_color_as(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() == -1)
        return;
    QString filename;
    filename = QFileDialog::getSaveFileName(
                this,"Save tracts color as",item(currentRow(),0)->text() + "_color.txt",
                "Color files (*.txt);;All files (*)");
    if(filename.isEmpty())
        return;

    std::string sfilename = filename.toLocal8Bit().begin();
    tract_models[uint32_t(currentRow())]->save_tracts_color_to_file(&*sfilename.begin());
}

void get_track_statistics(std::shared_ptr<fib_data> handle,
                          const std::vector<std::shared_ptr<TractModel> >& tract_models,
                          const std::vector<std::string>& track_name,
                          std::string& result)
{
    if(tract_models.empty())
        return;
    std::vector<std::vector<std::string> > track_results(tract_models.size());
    tipl::par_for(tract_models.size(),[&](unsigned int index)
    {
        std::string tmp,line;
        tract_models[index]->get_quantitative_info(handle,tmp);
        std::istringstream in(tmp);
        while(std::getline(in,line))
        {
            if(line.find("\t") == std::string::npos)
                continue;
            track_results[index].push_back(line);
        }
    });

    std::ostringstream out;
    out << "Tract Name\t";
    for(unsigned int index = 0;index < tract_models.size();++index)
        out << track_name[index] << "\t";
    out << std::endl;
    for(unsigned int index = 0;index < track_results[0].size();++index)
    {
        out << track_results[0][index];
        for(unsigned int i = 1;i < track_results.size();++i)
            out << track_results[i][index].substr(track_results[i][index].find("\t"));
        out << std::endl;
    }
    result = out.str();
}
std::vector<std::shared_ptr<TractModel> > TractTableWidget::get_checked_tracks(void)
{
    std::vector<std::shared_ptr<TractModel> > active_tracks;
    for(unsigned int index = 0;index < tract_models.size();++index)
        if(item(int(index),0)->checkState() == Qt::Checked)
            active_tracks.push_back(tract_models[index]);
    return active_tracks;
}
std::vector<std::string> TractTableWidget::get_checked_tracks_name(void) const
{
    std::vector<std::string> track_name;
    for(unsigned int index = 0;index < tract_models.size();++index)
        if(item(int(index),0)->checkState() == Qt::Checked)
            track_name.push_back(item(int(index),0)->text().toStdString());
    return track_name;
}
void TractTableWidget::show_tracts_statistics(void)
{
    if(tract_models.empty())
        return;
    std::string result;
    get_track_statistics(cur_tracking_window.handle,get_checked_tracks(),get_checked_tracks_name(),result);
    if(!result.empty())
        show_info_dialog("Tract Statistics",result);

}

bool TractTableWidget::command(QString cmd,QString param,QString param2)
{
    if(cmd == "save_all_tracts_to_dir")
    {
        progress prog_("save files");
        auto selected_tracts = get_checked_tracks();
        for(size_t index = 0;index < selected_tracts.size();++index)
        {
            std::string filename = param.toStdString();
            filename += "/";
            filename += item(int(index),0)->text().toStdString();
            filename += output_format().toStdString();
            selected_tracts[index]->save_tracts_to_file(filename.c_str());
        }
        return true;
    }
    if(cmd == "update_track")
    {
        for(int index = 0;index < int(tract_models.size());++index)
        {
            item(int(index),1)->setText(QString::number(tract_models[index]->get_visible_track_count()));
            item(int(index),2)->setText(QString::number(tract_models[index]->get_deleted_track_count()));
        }
        emit need_update();
        return true;
    }
    if(cmd == "run_tracking")
    {
        start_tracking();
        while(timer->isActive())
            fetch_tracts();
        emit need_update();
        return true;
    }
    if(cmd == "cut_by_slice")
    {
        cut_by_slice(param.toInt(),param2.toInt());
        return true;
    }
    if(cmd == "delete_all_tract")
    {
        setRowCount(0);
        thread_data.clear();
        tract_models.clear();
        emit need_update();
        return true;
    }
    if(cmd == "save_tracks")
    {
        return TractModel::save_all(param.toStdString().c_str(),
                             get_checked_tracks(),get_checked_tracks_name());
    }
    if(cmd == "load_track_color")
    {
        int index = currentRow();
        if(!param2.isEmpty())
        {
            index = param2.toInt();
            if(index < 0 || index >= tract_models.size())
            {
                std::cout << "Invalid track index:" << param2.toStdString() << std::endl;
                return false;
            }
        }
        std::string sfilename = param.toStdString().c_str();
        if(!tract_models[index]->load_tracts_color_from_file(&*sfilename.begin()))
        {
            std::cout << "cannot find or open " << sfilename << std::endl;
            return false;
        }
        cur_tracking_window.set_data("tract_color_style",1);//manual assigned
        emit need_update();
        return true;
    }
    return false;
}

void TractTableWidget::save_tracts_data_as(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() == -1)
        return;
    QAction *action = qobject_cast<QAction *>(sender());
    if(!action)
        return;
    QString filename = QFileDialog::getSaveFileName(
                this,"Save as",item(currentRow(),0)->text() + "_" + action->data().toString() + ".txt",
                "Text files (*.txt);;MATLAB file (*.mat);;TRK file (*.trk *.trk.gz);;All files (*)");
    if(filename.isEmpty())
        return;

    if(!tract_models[uint32_t(currentRow())]->save_data_to_file(
                    cur_tracking_window.handle,filename.toLocal8Bit().begin(),
                    action->data().toString().toLocal8Bit().begin()))
    {
        QMessageBox::information(this,"error","fail to save information");
    }
    else
        QMessageBox::information(this,"DSI Studio","file saved");
}


void TractTableWidget::merge_all(void)
{
    std::vector<unsigned int> merge_list;
    for(int index = 0;index < tract_models.size();++index)
        if(item(int(index),0)->checkState() == Qt::Checked)
            merge_list.push_back(index);
    if(merge_list.size() <= 1)
        return;

    for(int index = merge_list.size()-1;index >= 1;--index)
    {
        tract_models[merge_list[0]]->add(*tract_models[merge_list[index]]);
        delete_row(merge_list[index]);
    }
    item(merge_list[0],1)->setText(QString::number(tract_models[merge_list[0]]->get_visible_track_count()));
    item(merge_list[0],2)->setText(QString::number(tract_models[merge_list[0]]->get_deleted_track_count()));
}

void TractTableWidget::delete_row(int row)
{
    if(row >= tract_models.size())
        return;
    thread_data.erase(thread_data.begin()+row);
    tract_models.erase(tract_models.begin()+row);
    removeRow(row);
}

void TractTableWidget::copy_track(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() == -1)
        return;
    uint32_t old_row = uint32_t(currentRow());
    addNewTracts(item(currentRow(),0)->text() + "_copy");
    *(tract_models.back()) = *(tract_models[old_row]);
    item(currentRow(),1)->setText(QString::number(tract_models.back()->get_visible_track_count()));
    emit need_update();
}

void TractTableWidget::separate_deleted_track(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() == -1)
        return;
    std::vector<std::vector<float> > new_tracks;
    new_tracks.swap(tract_models[uint32_t(currentRow())]->get_deleted_tracts());
    if(new_tracks.empty())
        return;
    // clean the deleted tracks
    tract_models[uint32_t(currentRow())]->clear_deleted();
    item(currentRow(),1)->setText(QString::number(tract_models[uint32_t(currentRow())]->get_visible_track_count()));
    item(currentRow(),2)->setText(QString::number(tract_models[uint32_t(currentRow())]->get_deleted_track_count()));
    // add deleted tracks to a new entry
    addNewTracts(item(currentRow(),0)->text(),false);
    tract_models.back()->add_tracts(new_tracks);
    tract_models.back()->report = tract_models[uint32_t(currentRow())]->report;
    item(rowCount()-1,1)->setText(QString::number(tract_models.back()->get_visible_track_count()));
    item(rowCount()-1,2)->setText(QString::number(tract_models.back()->get_deleted_track_count()));
    emit need_update();
}
void TractTableWidget::sort_track_by_name(void)
{
    std::vector<std::string> name_list;
    for(int i= 0;i < rowCount();++i)
        name_list.push_back(item(i,0)->text().toStdString());
    for(int i= 0;i < rowCount()-1;++i)
    {
        int j = std::min_element(name_list.begin()+i,name_list.end())-name_list.begin();
        if(i == j)
            continue;
        std::swap(name_list[i],name_list[j]);
        for(unsigned int col = 0;col <= 3;++col)
        {
            QString tmp = item(i,col)->text();
            item(i,col)->setText(item(j,col)->text());
            item(j,col)->setText(tmp);
        }
        Qt::CheckState checked = item(i,0)->checkState();
        item(i,0)->setCheckState(item(j,0)->checkState());
        item(j,0)->setCheckState(checked);
        std::swap(thread_data[i],thread_data[j]);
        std::swap(tract_models[i],tract_models[j]);
    }
}
void TractTableWidget::merge_track_by_name(void)
{
    for(int i= 0;i < rowCount()-1;++i)
        for(int j= i+1;j < rowCount()-1;)
        if(item(i,0)->text() == item(j,0)->text())
        {
            tract_models[i]->add(*tract_models[j]);
            delete_row(j);
            item(i,1)->setText(QString::number(tract_models[i]->get_visible_track_count()));
            item(i,2)->setText(QString::number(tract_models[i]->get_deleted_track_count()));
        }
    else
        ++j;
}

void TractTableWidget::move_up(void)
{
    if(currentRow() > 0)
    {
        for(unsigned int col = 0;col <= 3;++col)
        {
            QString tmp = item(currentRow(),col)->text();
            item(currentRow(),col)->setText(item(currentRow()-1,col)->text());
            item(currentRow()-1,col)->setText(tmp);
        }
        Qt::CheckState checked = item(currentRow(),0)->checkState();
        item(currentRow(),0)->setCheckState(item(currentRow()-1,0)->checkState());
        item(currentRow()-1,0)->setCheckState(checked);
        std::swap(thread_data[uint32_t(currentRow())],thread_data[currentRow()-1]);
        std::swap(tract_models[uint32_t(currentRow())],tract_models[currentRow()-1]);
        setCurrentCell(currentRow()-1,0);
    }
    emit need_update();
}

void TractTableWidget::move_down(void)
{
    if(currentRow()+1 < tract_models.size())
    {
        for(unsigned int col = 0;col <= 3;++col)
        {
            QString tmp = item(currentRow(),col)->text();
            item(currentRow(),col)->setText(item(currentRow()+1,col)->text());
            item(currentRow()+1,col)->setText(tmp);
        }
        Qt::CheckState checked = item(currentRow(),0)->checkState();
        item(currentRow(),0)->setCheckState(item(currentRow()+1,0)->checkState());
        item(currentRow()+1,0)->setCheckState(checked);
        std::swap(thread_data[uint32_t(currentRow())],thread_data[currentRow()+1]);
        std::swap(tract_models[uint32_t(currentRow())],tract_models[currentRow()+1]);
        setCurrentCell(currentRow()+1,0);
    }
    emit need_update();
}


void TractTableWidget::delete_tract(void)
{
    if(progress::running())
    {
        QMessageBox::information(this,"Error","Please wait for the termination of data processing");
        return;
    }
    delete_row(currentRow());
    emit need_update();
}

void TractTableWidget::delete_all_tract(void)
{
    if(progress::running())
    {
        QMessageBox::information(this,"Error","Please wait for the termination of data processing");
        return;
    }
    command("delete_all_tract");
}

void TractTableWidget::delete_repeated(void)
{
    float distance = 1.0;
    bool ok;
    distance = QInputDialog::getDouble(this,
        "DSI Studio","Distance threshold (voxels)", distance,0,500,1,&ok);
    if (!ok)
        return;
    progress prog_("deleting tracks");
    for(int i = 0;progress::at(i,tract_models.size());++i)
    {
        if(item(i,0)->checkState() == Qt::Checked)
            tract_models[i]->delete_repeated(distance);
        item(i,1)->setText(QString::number(tract_models[i]->get_visible_track_count()));
        item(i,2)->setText(QString::number(tract_models[i]->get_deleted_track_count()));
    }
    emit need_update();
}


void TractTableWidget::delete_branches(void)
{
    progress prog_("deleting branches");
    for(int i = 0;progress::at(i,tract_models.size());++i)
    {
        if(item(i,0)->checkState() == Qt::Checked)
            tract_models[i]->delete_branch();
        item(i,1)->setText(QString::number(tract_models[i]->get_visible_track_count()));
        item(i,2)->setText(QString::number(tract_models[i]->get_deleted_track_count()));
    }
    emit need_update();
}

void TractTableWidget::resample_step_size(void)
{
    if(currentRow() >= int(tract_models.size()) || currentRow() == -1)
        return;
    float new_step = 0.5f;
    bool ok;
    new_step = float(QInputDialog::getDouble(this,
        "DSI Studio","New step size (voxels)",double(new_step),0.0,5.0,1,&ok));
    if (!ok)
        return;

    progress prog_("resample tracks");
    auto selected_tracts = get_checked_tracks();
    for(size_t i = 0;progress::at(i,selected_tracts.size());++i)
        selected_tracts[i]->resample(new_step);
    emit need_update();
}

void TractTableWidget::delete_by_length(void)
{
    progress prog_("filtering tracks");

    float threshold = 60;
    bool ok;
    threshold = QInputDialog::getDouble(this,
        "DSI Studio","Length threshold in mm:", threshold,0,500,1,&ok);
    if (!ok)
        return;

    for(int i = 0;progress::at(i,tract_models.size());++i)
    {
        if(item(i,0)->checkState() == Qt::Checked)
            tract_models[i]->delete_by_length(threshold);
        item(i,1)->setText(QString::number(tract_models[i]->get_visible_track_count()));
        item(i,2)->setText(QString::number(tract_models[i]->get_deleted_track_count()));
    }
    emit need_update();
}
void TractTableWidget::reconnect_track(void)
{
    int cur_row = currentRow();
    if(cur_row < 0 || item(cur_row,0)->checkState() != Qt::Checked)
        return;
    bool ok;
    QString result = QInputDialog::getText(this,"DSI Studio","Assign maximum bridging distance (in voxels) and angles (degrees)",
                                           QLineEdit::Normal,"4 30",&ok);

    if(!ok)
        return;
    std::istringstream in(result.toStdString());
    float dis,angle;
    in >> dis >> angle;
    if(dis <= 2.0f || angle <= 0.0f)
        return;
    tract_models[uint32_t(cur_row)]->reconnect_track(dis,std::cos(angle*3.14159265358979323846f/180.0f));
    item(cur_row,1)->setText(QString::number(tract_models[uint32_t(cur_row)]->get_visible_track_count()));
    emit need_update();
}
void TractTableWidget::edit_tracts(void)
{
    QRgb color = 0;
    if(edit_option == paint)
        color = QColorDialog::getColor(Qt::red,this,"Select color",QColorDialog::ShowAlphaChannel).rgb();
    for(unsigned int index = 0;index < tract_models.size();++index)
    {
        if(item(int(index),0)->checkState() != Qt::Checked)
            continue;
        switch(edit_option)
        {
        case select:
        case del:
            tract_models[index]->cull(
                             cur_tracking_window.glWidget->angular_selection ?
                             cur_tracking_window["tract_sel_angle"].toFloat():0.0,
                             cur_tracking_window.glWidget->dirs,
                             cur_tracking_window.glWidget->pos,edit_option == del);
            break;
        case cut:
            tract_models[index]->cut(
                        cur_tracking_window.glWidget->angular_selection ?
                        cur_tracking_window["tract_sel_angle"].toFloat():0.0,
                             cur_tracking_window.glWidget->dirs,
                             cur_tracking_window.glWidget->pos);
            break;
        case paint:
            tract_models[index]->paint(
                        cur_tracking_window.glWidget->angular_selection ?
                        cur_tracking_window["tract_sel_angle"].toFloat():0.0,
                             cur_tracking_window.glWidget->dirs,
                             cur_tracking_window.glWidget->pos,color);
            cur_tracking_window.set_data("tract_color_style",1);//manual assigned
            break;
        default:
            break;
        }
        item(int(index),1)->setText(QString::number(tract_models[index]->get_visible_track_count()));
        item(int(index),2)->setText(QString::number(tract_models[index]->get_deleted_track_count()));
    }
    emit need_update();
}

void TractTableWidget::undo_tracts(void)
{
    for(unsigned int index = 0;index < tract_models.size();++index)
    {
        if(item(int(index),0)->checkState() != Qt::Checked)
            continue;
        tract_models[index]->undo();
        item(int(index),1)->setText(QString::number(tract_models[index]->get_visible_track_count()));
        item(int(index),2)->setText(QString::number(tract_models[index]->get_deleted_track_count()));
    }
    emit need_update();
}
void TractTableWidget::cut_by_slice(unsigned char dim,bool greater)
{
    for(unsigned int index = 0;index < tract_models.size();++index)
    {
        if(item(int(index),0)->checkState() != Qt::Checked)
            continue;
        if(cur_tracking_window.current_slice->is_diffusion_space)
            tract_models[index]->cut_by_slice(dim,cur_tracking_window.current_slice->slice_pos[dim],greater);
        else
        {
            tract_models[index]->cut_by_slice(dim,cur_tracking_window.current_slice->slice_pos[dim],greater,
                                                  &cur_tracking_window.current_slice->invT);
        }
        item(int(index),1)->setText(QString::number(tract_models[index]->get_visible_track_count()));
        item(int(index),2)->setText(QString::number(tract_models[index]->get_deleted_track_count()));
    }
    emit need_update();
}

void TractTableWidget::redo_tracts(void)
{
    for(unsigned int index = 0;index < tract_models.size();++index)
    {
        if(item(int(index),0)->checkState() != Qt::Checked)
            continue;
        tract_models[index]->redo();
        item(int(index),1)->setText(QString::number(tract_models[index]->get_visible_track_count()));
        item(int(index),2)->setText(QString::number(tract_models[index]->get_deleted_track_count()));
    }
    emit need_update();
}

void TractTableWidget::trim_tracts(void)
{
    for(unsigned int index = 0;index < tract_models.size();++index)
    {
        if(item(int(index),0)->checkState() != Qt::Checked)
            continue;
        tract_models[index]->trim();
        item(int(index),1)->setText(QString::number(tract_models[index]->get_visible_track_count()));
        item(int(index),2)->setText(QString::number(tract_models[index]->get_deleted_track_count()));
    }
    emit need_update();
}

void TractTableWidget::export_tract_density(tipl::shape<3> dim,
                                            tipl::vector<3,float> vs,
                                            tipl::matrix<4,4> transformation,bool color,bool end_point)
{
    QString filename;
    if(color)
    {
        filename = QFileDialog::getSaveFileName(
                this,"Save Images files",item(currentRow(),0)->text()+".nii.gz",
                "Image files (*.png *.bmp *nii.gz *.nii *.jpg *.tif);;All files (*)");
        if(filename.isEmpty())
            return;
    }
    else
    {
        filename = QFileDialog::getSaveFileName(
                    this,"Save as",item(currentRow(),0)->text()+".nii.gz",
                    "NIFTI files (*nii.gz *.nii);;MAT File (*.mat);;");
        if(filename.isEmpty())
            return;
    }
    if(TractModel::export_tdi(filename.toStdString().c_str(),get_checked_tracks(),dim,vs,transformation,color,end_point))
        QMessageBox::information(this,"DSI Studio","File saved");
    else
        QMessageBox::critical(this,"ERROR","Failed to save file");
}


