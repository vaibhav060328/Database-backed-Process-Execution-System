import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
  BarChart,
  Bar,
} from 'recharts'

const COLORS = {
  queued:    '#38bdf8',
  running:   '#f59e0b',
  done:      '#22c55e',
  failed:    '#ef4444',
  total:     '#7c6bff',
}

const CustomTooltip = ({ active, payload, label }) => {
  if (!active || !payload?.length) return null
  return (
    <div style={{
      background: 'var(--bg-elevated)',
      border: '1px solid var(--border)',
      borderRadius: 'var(--radius-md)',
      padding: '10px 14px',
      fontSize: '0.8rem',
    }}>
      <p style={{ color: 'var(--text-muted)', marginBottom: 6, fontWeight: 600 }}>{label}</p>
      {payload.map((entry) => (
        <div key={entry.dataKey} style={{ display: 'flex', justifyContent: 'space-between', gap: 16, color: entry.color }}>
          <span style={{ textTransform: 'capitalize' }}>{entry.name}</span>
          <span style={{ fontWeight: 600 }}>{entry.value}</span>
        </div>
      ))}
    </div>
  )
}

/**
 * StatsChart
 * @param {{
 *   data: Array<Object>,
 *   type?: 'line'|'bar',
 *   lines?: Array<{ dataKey: string, color?: string }>,
 *   xKey?: string,
 * }} props
 */
export default function StatsChart({
  data = [],
  type = 'line',
  lines = [
    { dataKey: 'done',    color: COLORS.done },
    { dataKey: 'running', color: COLORS.running },
    { dataKey: 'failed',  color: COLORS.failed },
    { dataKey: 'queued',  color: COLORS.queued },
  ],
  xKey = 'time',
}) {
  const ChartComponent = type === 'bar' ? BarChart : LineChart

  return (
    <ResponsiveContainer width="100%" height={260}>
      <ChartComponent data={data} margin={{ top: 8, right: 8, left: -20, bottom: 0 }}>
        <CartesianGrid strokeDasharray="3 3" stroke="var(--border)" vertical={false} />
        <XAxis
          dataKey={xKey}
          tick={{ fill: 'var(--text-muted)', fontSize: 11, fontFamily: 'Inter, sans-serif' }}
          axisLine={false}
          tickLine={false}
        />
        <YAxis
          tick={{ fill: 'var(--text-muted)', fontSize: 11, fontFamily: 'Inter, sans-serif' }}
          axisLine={false}
          tickLine={false}
          allowDecimals={false}
        />
        <Tooltip content={<CustomTooltip />} />
        <Legend
          iconType="circle"
          iconSize={8}
          wrapperStyle={{ fontSize: '0.78rem', paddingTop: 12, color: 'var(--text-secondary)', textTransform: 'capitalize' }}
        />
        {lines.map(({ dataKey, color }) =>
          type === 'bar' ? (
            <Bar
              key={dataKey}
              dataKey={dataKey}
              name={dataKey}
              fill={color ?? COLORS[dataKey] ?? COLORS.total}
              radius={[3, 3, 0, 0]}
              maxBarSize={28}
            />
          ) : (
            <Line
              key={dataKey}
              type="monotone"
              dataKey={dataKey}
              name={dataKey}
              stroke={color ?? COLORS[dataKey] ?? COLORS.total}
              strokeWidth={2}
              dot={false}
              activeDot={{ r: 4, strokeWidth: 0 }}
            />
          )
        )}
      </ChartComponent>
    </ResponsiveContainer>
  )
}
