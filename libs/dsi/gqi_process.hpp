#ifndef DDI_PROCESS_HPP
#define DDI_PROCESS_HPP
#define _USE_MATH_DEFINES
#include <math.h>
#include <boost/math/special_functions/sinc.hpp>
#include "basic_process.hpp"
#include "basic_voxel.hpp"
#include "image_model.hpp"
#include "odf_process.hpp"

float base_function(float theta);
class GQI_Recon  : public BaseProcess
{
public:// recorded for scheme balanced
    std::vector<tipl::vector<3,float> > q_vectors_time;
public:
    std::vector<float> sinc_ql;
public:
    virtual void init(Voxel& voxel)
    {
        if(voxel.qsdr)
            voxel.calculate_q_vec_t(q_vectors_time);
        else
            voxel.calculate_sinc_ql(sinc_ql);
    }
    virtual void run(Voxel& voxel, VoxelData& data)
    {
        if(voxel.half_sphere)
            data.space[0] *= 0.5f;
        // add rotation from QSDR or gradient nonlinearity
        if(voxel.qsdr)
        {
            std::vector<float> sinc_ql_(data.odf.size()*data.space.size());
            for (unsigned int j = 0,index = 0; j < data.odf.size(); ++j)
            {
                tipl::vector<3,float> from(voxel.ti.vertices[j]);
                from.rotate(data.jacobian);
                from.normalize();
                if(voxel.r2_weighted)
                    for (unsigned int i = 0; i < data.space.size(); ++i,++index)
                        sinc_ql_[index] = base_function(q_vectors_time[i]*from);
                else
                    for (unsigned int i = 0; i < data.space.size(); ++i,++index)
                        sinc_ql_[index] = boost::math::sinc_pi(q_vectors_time[i]*from);

            }
            tipl::mat::vector_product(&*sinc_ql_.begin(),&*data.space.begin(),&*data.odf.begin(),
                                          tipl::dyndim(uint32_t(data.odf.size()),uint32_t(data.space.size())));
        }
        else
            tipl::mat::vector_product(&*sinc_ql.begin(),&*data.space.begin(),&*data.odf.begin(),
                                    tipl::dyndim(uint32_t(data.odf.size()),uint32_t(data.space.size())));
    }
};

class dGQI_Recon : public BaseProcess{
    BalanceScheme bs;
    GQI_Recon gr;
public:
    virtual void init(Voxel& v)
    {
        if(!v.compare_voxel)
            return;
        v.bvalues = v.compare_voxel->bvalues;
        v.bvectors = v.compare_voxel->bvectors;
        bs.init(v);
        gr.init(v);
    }
    virtual void run(Voxel& voxel, VoxelData& data)
    {
        if(!voxel.compare_voxel)
            return;
        if(voxel.compare_voxel->dwi_data[0][data.voxel_index] == 0) // no data in the compared dataset
        {
            data.odf1.clear();
            data.odf1.resize(data.odf.size());
            data.odf = data.odf1;
            data.odf2 = data.odf1;
            return;
        }
        data.space.resize(voxel.compare_voxel->dwi_data.size());
        for (unsigned int index = 0; index < data.space.size(); ++index)
            data.space[index] = voxel.compare_voxel->dwi_data[index][data.voxel_index];

        //temporarily store baseline odf here
        data.odf1 = data.odf;
        tipl::minus_constant(data.odf1,*std::min_element(data.odf1.begin(),data.odf1.end()));

        bs.run(voxel,data);
        gr.run(voxel,data);

        data.odf2 = data.odf;
        tipl::minus_constant(data.odf2,*std::min_element(data.odf2.begin(),data.odf2.end()));

        tipl::add(data.odf,data.odf1);
        tipl::multiply_constant(data.odf,0.5f);
        //float qa = *std::max_element(data.odf.begin(),data.odf.end());
        //if(qa > voxel.z0)
        //    voxel.z0 = qa; // z0 is the maximum qa in the baseline

    }
    virtual void end(Voxel&,gz_mat_write&) {}
};

class HGQI_Recon  : public BaseProcess
{
public:
    std::vector<float> sinc_ql;
public:
    bool hgqi = false;
    std::vector<float> hraw;
    std::vector<int> offset;
    std::vector<float> scaling;
public:
    virtual void init(Voxel& voxel)
    {
        if(voxel.bvalues.size() != 1)
        {
            hgqi = false;
            return;
        }
        hgqi = true;
        hraw.resize(voxel.dim.size());
        int range = 2;
        for(int dz = 0;dz <= range;++dz) // half sphere
            for(int dy = -range;dy <= range;++dy)
                for(int dx = -range;dx <= range;++dx)
                {
                    int r2 = dx*dx+dy*dy+dz*dz;
                    voxel.bvalues.push_back(r2*500);
                    tipl::vector<3> dir(dx,dy,dz);
                    dir.normalize();
                    voxel.bvectors.push_back(dir);
                    offset.push_back(dx + dy*int(voxel.dim.width()) + dz*int(voxel.dim.plane_size()));
                    scaling.push_back(float(std::exp(-r2)));
                }
        voxel.calculate_sinc_ql(sinc_ql);
    }
    virtual void run(Voxel& voxel, VoxelData& data)
    {
        if(!hgqi)
            return;
        if(int(data.space[0]) == 0)
        {
            hraw[data.voxel_index] = 0;
            std::fill(data.odf.begin(),data.odf.end(),0.0f);
            return;
        }
        hraw[data.voxel_index] = data.space[0];
        auto I = tipl::make_image(voxel.dwi_data[0],voxel.dim);
        data.space.resize(voxel.bvalues.size());
        for(size_t i = 1;i < voxel.bvalues.size();++i)
        {
            int pos1 = int(data.voxel_index);
            int pos2 = int(data.voxel_index);
            pos1 += int(offset[i-1]);
            pos2 -= int(offset[i-1]);
            if(pos1 < 0 || pos1 >= int(voxel.dim.size()))
                pos1 = pos2;
            if(pos2 < 0 || pos2 >= int(voxel.dim.size()))
                pos2 = pos1;
            data.space[i] = std::fabs(data.space[0]-(I[pos1]+I[pos2])/2.0f)*scaling[i-1];
        }

        tipl::mat::vector_product(&*sinc_ql.begin(),&*data.space.begin(),&*data.odf.begin(),
                                    tipl::dyndim(uint32_t(data.odf.size()),uint32_t(data.space.size())));
    }
    virtual void end(Voxel& voxel,gz_mat_write& mat_writer)
    {
        if(hgqi)
            mat_writer.write("hraw",&hraw[0],voxel.dim.plane_size(),voxel.dim.depth());
    }
};

class SchemeConverter : public BaseProcess
{
    GQI_Recon from,to;
    std::vector<int> piv;
    std::vector<tipl::vector<3,float> > bvectors;
    std::vector<float> bvalues;
    std::vector<std::vector<unsigned short> > dwi;
    std::vector<unsigned short> b0;
    std::vector<float> A,Rt;
    unsigned int total_value;
    unsigned int total_negative_value;
public:
    virtual void init(Voxel& voxel)
    {

        if(!voxel.file_name.empty())
        {
            std::ifstream read(voxel.file_name.c_str());
            std::vector<float> values;
            std::copy(std::istream_iterator<float>(read),std::istream_iterator<float>(),std::back_inserter(values));
            for(unsigned int i = 0;i < values.size()/4;++i)
            {
                if(values[i*4] == 0.0f)
                    continue;
                bvalues.push_back(voxel.param[1]);
                bvectors.push_back(tipl::vector<3,float>(values[i*4+1],values[i*4+2],values[i*4+3]));
            }
        }
        else
        {
            bvalues.resize(voxel.ti.half_vertices_count);
            bvectors.resize(voxel.ti.half_vertices_count);
            for (unsigned int index = 0; index < bvectors.size(); ++index)
                bvectors[index] = voxel.ti.vertices[index];
            std::fill(bvalues.begin(),bvalues.end(),voxel.param[1]); // set the output b-value
        }

        //allocated output image space
        dwi.resize(bvalues.size());
        for(unsigned int index = 0;index < dwi.size();++index)
            dwi[index].resize(voxel.dim.size());


        from.init(voxel);
        voxel.bvalues.swap(bvalues);
        voxel.bvectors.swap(bvectors);
        to.init(voxel);
        voxel.bvalues.swap(bvalues);
        voxel.bvectors.swap(bvectors);

        b0.resize(voxel.dim.size());

        Rt.resize(dwi.size()*dwi.size());
        tipl::mat::transpose(&*to.sinc_ql.begin(),&*Rt.begin(),tipl::dyndim(dwi.size(),dwi.size()));
        A.resize(dwi.size()*dwi.size());
        piv.resize(dwi.size());
        tipl::mat::product_transpose(&*Rt.begin(),&*Rt.begin(),&*A.begin(),
                                       tipl::dyndim(dwi.size(),dwi.size()),tipl::dyndim(dwi.size(),dwi.size()));
        float max_value = *std::max_element(A.begin(),A.end());
        for (unsigned int i = 0,index = 0; i < dwi.size(); ++i,index += dwi.size() + 1)
            A[index] += max_value*voxel.param[2];
        tipl::mat::lu_decomposition(A.begin(),piv.begin(),tipl::dyndim(dwi.size(),dwi.size()));

        total_negative_value = 0;
        total_value = 0;

        voxel.recon_report
                << " The converted HARDI has a total of " << dwi.size() << " diffusion sampling directions.";
    }

    virtual void run(Voxel& voxel, VoxelData& data)
    {
        {
            b0[data.voxel_index] = uint16_t(data.space[0]);
            data.space[0] = 0;
        }
        from.run(voxel,data);
        std::vector<float> hardi_data(dwi.size()),tmp(dwi.size());
        tipl::mat::vector_product(&*Rt.begin(),&*data.odf.begin(),&*tmp.begin(),tipl::dyndim(dwi.size(),dwi.size()));
        tipl::mat::lu_solve(&*A.begin(),&*piv.begin(),&*tmp.begin(),&*hardi_data.begin(),tipl::dyndim(dwi.size(),dwi.size()));
        for(unsigned int index = 0;index < dwi.size();++index)
        {
            if(hardi_data[index] < 0.0f)
                ++total_negative_value;
            else
                dwi[index][data.voxel_index] = uint16_t(hardi_data[index]);
            ++total_value;
        }
    }
    virtual void end(Voxel& voxel,gz_mat_write& mat_writer)
    {
        voxel.recon_report
                << " The percentage of the negative signals was " << float(100.0f)*total_negative_value/float(total_value) << "%.";
        std::vector<float> b_table(4); // space for b0
        for (unsigned int index = 0;index < bvectors.size();++index)
        {
            b_table.push_back(bvalues.front());
            std::copy(bvectors[index].begin(),bvectors[index].end(),std::back_inserter(b_table));
        }
        mat_writer.write("b_table",b_table,4);
        unsigned int image_num = 0;
        if(!b0.empty())
        {
            mat_writer.write("image0",b0,uint32_t(voxel.dim.plane_size()));
            ++image_num;
        }
        for (unsigned int index = 0;index < dwi.size();++index)
        {
            std::ostringstream out;
            out << "image" << image_num;
            mat_writer.write(out.str().c_str(),dwi[index],uint32_t(voxel.dim.plane_size()));
            ++image_num;
        }
    }
};

float sinint(float x)
{
    bool sgn = x > 0;
    x = std::fabs(x);
    float eps = 1e-15f;
    float x2 = x*x;
    float si;
    if(x == 0.0f)
        return 0.0f;
    if(x <= 16.0f)
    {
        float xr = x;
        si = x;
        for(unsigned int k = 1;k <= 40;++k)
        {
            si += (xr *= -0.5f*float(2*k-1)/float(k)/float(4*k*(k+1)+1)*x2);
            if(std::fabs(xr) < std::fabs(si)*eps)
                break;
        }
        return sgn ? si:-si;
    }

    if(x <= 32.0f)
    {
        unsigned int m = uint32_t(std::floor(47.2f+0.82f*x));
        std::vector<float> bj(m+1);
        float xa1 = 0.0f;
        float xa0 = 1.0e-40f;
        for(unsigned int k=m;k>=1;--k)
        {
            float xa = 4.0f*float(k)*xa0/x-xa1;
            bj[k-1] = xa;
            xa1 = xa0;
            xa0 = xa;
        }
        float xs = bj[0];
        for(unsigned int k=3;k <= m;k += 2)
            xs += 2.0f*bj[k-1];
        for(unsigned int k=0;k < m;++k)
            bj[k] /= xs;
        float xr = 1.0f;
        float xg1 = bj[0];
        for(unsigned int k=2;k <= m;++k)
            xg1 += bj[k-1]*(xr *= 0.25f*(2*k-3)*(2*k-3)/((k-1)*(2*k-1)*(2*k-1))*x);
        xr = 1.0;
        float xg2 = bj[0];
        for(unsigned int k=2;k <= m;++k)
            xg2 += bj[k-1]*(xr *= 0.25f*(2*k-5)*(2*k-5)/((k-1)*(2*k-3)*(2*k-3))*x);
        si = x*std::cos(x/2.0f)*xg1+2.0f*std::sin(x/2.0f)*xg2-std::sin(x);
        return sgn ? si:-si;
    }

    float xr = 1.0f;
    float xf = 1.0f;
    for(unsigned int k=1;k <= 9;++k)
        xf += (xr *= -2.0f*k*(2*k-1)/x2);
    xr = 1.0f/x;
    float xg = xr;
    for(unsigned int k=1;k <= 8;++k)
        xg += (xr *= -2.0f*(2*k+1)*k/x2);
    si = 1.570796326794897f-xf*std::cos(x)/x-xg*std::sin(x)/x;
    return sgn ? si:-si;
}


class QSpaceSpectral  : public BaseProcess
{
public:
    static const int max_length = 50; // 50 microns
    std::vector<unsigned int> b0_images;
    std::vector<std::vector<float> > cdf,dis,cdfw,disw;
public:
    virtual void init(Voxel& voxel)
    {
        b0_images.clear();
        for(unsigned int index = 0;index < voxel.bvalues.size();++index)
            if(voxel.bvalues[index] == 0.0f)
                b0_images.push_back(index);

        float diffusion_time = voxel.param[1];
        float diffusion_length = std::sqrt(6.0f*3.0f*diffusion_time); // sqrt(6Dt)
        dis.clear();
        cdf.clear();
        disw.clear();
        cdfw.clear();
        dis.resize(max_length);
        cdf.resize(max_length);
        disw.resize(max_length);
        cdfw.resize(max_length);
        for(unsigned int n = 0;n < max_length;++n) // from 0 micron to 49 microns
        {
            // calculate the diffusion length ratio
            float sigma = float(n)/diffusion_length;

            dis[n].resize(voxel.dim.size());
            cdf[n].resize(voxel.dim.size());

            disw[n].resize(voxel.bvalues.size());
            cdfw[n].resize(voxel.bvalues.size());
            for(unsigned int index = 0;index < voxel.bvalues.size();++index)
            {
                // 2pi*L*q = sigma*sqrt(6D*b_value)
                float lq_2pi = sigma*std::sqrt(voxel.bvalues[index]*0.018f);
                disw[n][index] = boost::math::sinc_pi(lq_2pi);
                cdfw[n][index] = (voxel.bvalues[index] == 0.0f ?
                                 sigma : sinint(lq_2pi)/std::sqrt(voxel.bvalues[index]*0.018f));
            }
        }
    }
    virtual void run(Voxel& voxel, VoxelData& data)
    {
        if(b0_images.size() == 1 && voxel.half_sphere)
            data.space[b0_images.front()] *= 0.5f;
        for(unsigned int index = 0;index < max_length;++index)
        {
            dis[index][data.voxel_index] =
                    tipl::vec::dot(data.space.begin(),data.space.end(),disw[index].begin());
            cdf[index][data.voxel_index] =
                    tipl::vec::dot(data.space.begin(),data.space.end(),cdfw[index].begin());
            // make sure that cdf is increamental
            if(index && cdf[index][data.voxel_index] < cdf[index-1][data.voxel_index])
                cdf[index][data.voxel_index] = cdf[index-1][data.voxel_index];
            // make sure that dis is positive
            if(dis[index][data.voxel_index] < 0.0f)
                dis[index][data.voxel_index] = 0.0f;
        }
    }
    virtual void end(Voxel& voxel,gz_mat_write& mat_writer)
    {
        for(unsigned int index = 0;index < max_length;++index)
        {
            std::ostringstream out;
            out << "pdf_" << index << "um";
            mat_writer.write(out.str().c_str(),dis[index],uint32_t(voxel.dim.plane_size()));

        }
        for(unsigned int index = 0;index < max_length;++index)
        {
            std::ostringstream out;
            out << "cdf_" << index << "um";
            mat_writer.write(out.str().c_str(),cdf[index],uint32_t(voxel.dim.plane_size()));
        }
        mat_writer.write("fa0",dis[0],uint32_t(voxel.dim.plane_size()));
        std::vector<short> index0(voxel.dim.size());
        mat_writer.write("index0",index0,uint32_t(voxel.dim.plane_size()));
    }
};

class RDI_Recon  : public BaseProcess
{
private:
    std::vector<std::vector<float> > rdi_weightings;
public:
    virtual void init(Voxel& voxel)
    {
        if(!voxel.needs("rdi"))
            return;
        float sigma = voxel.param[0]; //optimal 1.24
        for(float L = 0.2f;L <= sigma;L+= 0.2f)
        {
            rdi_weightings.push_back(std::vector<float>(voxel.bvalues.size()));
            for(unsigned int index = 0;index < voxel.bvalues.size();++index)
            {
                float q = std::sqrt(voxel.bvalues[index]*0.018f);
                rdi_weightings.back()[index] = (q > 0)? sinint(L*q)/q:L;
            }
        }
    }
    virtual void run(Voxel&, VoxelData& data)
    {
        if(rdi_weightings.empty())
            return;
        float last_value = 0;
        std::vector<float> rdi_values(rdi_weightings.size());
        for(unsigned int index = 0;index < rdi_weightings.size();++index)
        {
            // force incremental
            rdi_values[index] = std::max<float>(last_value,tipl::vec::dot(rdi_weightings[index].begin(),rdi_weightings[index].end(),data.space.begin()));
            last_value = rdi_values[index];
        }
        data.rdi.swap(rdi_values);
    }
};
#endif//DDI_PROCESS_HPP
